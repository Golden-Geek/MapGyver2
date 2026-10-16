// SPDX-License-Identifier: GPL-3.0-or-later
import type { CommandDocument, MediaTime, ParameterWrite, ProjectDocument, RegistryDocument, WriteDocument } from './generated/model';
export type Revision = string;
export interface Renderer {available: boolean; reason: string; appliedRevision: Revision | null; showTime: MediaTime | null; frame: Revision | null}
export interface Persistence {available: boolean; reason: string; savedRevision: Revision | null}
export interface Snapshot {
  type: 'snapshot'; serviceId: string; authoredRevision: Revision; eventSequence: Revision;
  editOwner: string | null; isEditor: boolean; ownershipGeneration: Revision;
  data: {project: ProjectDocument; registry: RegistryDocument}; renderer: Renderer; persistence: Persistence;
}
interface Hello {type: 'hello'; protocolVersion: 1; serviceId: string; clientId: string; sessionId: string; nextSequence: Revision; snapshot: Snapshot}
interface Events {type: 'events'; resyncRequired: boolean; cursor: Revision; snapshot: Snapshot | null; events: Array<{sequence: Revision; kind: string}>}
export interface Reply {
  type: 'reply'; protocolVersion: 1; requestId: string | null; sequence: Revision | null;
  consumed: boolean; replayed: boolean; status: 'accepted' | 'rejected'; authoredRevision: Revision;
  error: {code: string; message: string} | null; payload: unknown; renderer: Renderer; persistence: Persistence;
}
type Payloads = {
  transaction: {commands: CommandDocument[]}; parameters: {writes: WriteDocument[]};
  snapshot: Record<string, never>; registry: Record<string, never>; undo: Record<string, never>; redo: Record<string, never>;
  subscribe: {afterEvent: Revision | null}; unsubscribe: Record<string, never>;
  claimEdit: {expectedOwnershipGeneration: Revision}; releaseEdit: {expectedOwnershipGeneration: Revision};
  transferEdit: {expectedOwnershipGeneration: Revision; targetSession: string};
};
interface Pending {operation: keyof Payloads; payloadJson: string; cost: number; revision: Revision | null; request?: string; sequence?: bigint; resolve: (reply: Reply) => void; reject: (error: Error) => void}
export interface View {status: string; snapshot: Snapshot | null; error: string; busy: boolean; reconnects: number}

// One in-flight request, bounded pending queue, exact retry bytes for lost ACK.
// Browser reload obtains a full snapshot; it never recreates an uncertain action.
export class ControlClient {
  readonly view: View = {status: 'Connecting', snapshot: null, error: '', busy: false, reconnects: 0};
  private socket: WebSocket | null = null;
  private timer: ReturnType<typeof setTimeout> | undefined;
  private replyTimer: ReturnType<typeof setTimeout> | undefined;
  private stopped = false;
  private ready = false;
  private attempt = 0;
  private hello: Hello | null = null;
  private next = 0n;
  private pending: Pending | null = null;
  private queue: Pending[] = [];
  private queuedBytes = 0;
  private refreshing = false;
  private readonly clientId: string;
  private sessionId: string | null;
  private readonly storageKey: string;
  constructor(readonly url: string, private readonly capability: string, private readonly changed: (view: View) => void) {
    this.storageKey = 'mapgyver.client.'+url;
    let saved: {clientId?: string; sessionId?: string} = {};
    try {saved = JSON.parse(sessionStorage.getItem(this.storageKey) || '{}');} catch { /* optional browser session storage */ }
    this.clientId = saved.clientId || 'browser-'+crypto.randomUUID(); this.sessionId = saved.sessionId || null;
    this.connect();
  }
  private emit() {this.view.busy = this.pending !== null || this.queue.length > 0; this.changed({...this.view});}
  private connect() {
    if (this.stopped) return;
    this.ready = false; this.view.status = this.attempt ? 'Reconnecting' : 'Connecting'; this.emit();
    const socket = new WebSocket(this.url, ['mapgyver.control.v1', 'token.'+this.capability]); this.socket = socket;
    socket.onopen = () => {
      socket.send(JSON.stringify({protocolVersion: 1, clientId: this.clientId, resumeSessionId: this.sessionId, requestEdit: true}));
      this.deadline();
    };
    socket.onmessage = event => {
      try {this.receive(JSON.parse(String(event.data)));}
      catch (error) {this.view.error = String(error); socket.close();}
    };
    socket.onclose = () => {
      if (this.socket !== socket || this.stopped) return;
      clearTimeout(this.replyTimer);
      this.ready = false; this.view.status = 'Reconnecting'; this.view.reconnects++;
      this.emit(); this.timer = setTimeout(() => this.connect(), Math.min(5000, 250*2**Math.min(this.attempt++, 5)));
    };
    socket.onerror = () => {this.view.error = 'Connection failed. Check the engine and launch link.'; this.emit();};
  }
  private receive(message: Hello | Reply | {type: 'notification'; payload: Events} | {type: 'hello_error'; error: {code: string; message: string}}) {
    if ('protocolVersion' in message && message.protocolVersion !== 1) throw new Error('Unsupported engine protocol');
    if (message.type === 'hello_error') {
      clearTimeout(this.replyTimer);
      this.view.error = message.error.message;
      if (message.error.code === 'session_expired') {this.sessionId = null; this.abandon('Session expired; uncertain changes were not retried.');}
      this.emit(); return;
    }
    if (message.type === 'hello') {
      clearTimeout(this.replyTimer);
      if (this.hello && this.hello.serviceId !== message.serviceId) this.abandon('Engine restarted; uncertain changes were not retried.');
      this.hello = message; this.sessionId = message.sessionId; this.next = BigInt(message.nextSequence);
      try {sessionStorage.setItem(this.storageKey, JSON.stringify({clientId: this.clientId, sessionId: this.sessionId}));} catch { /* reconnect still works in memory */ }
      this.ready = true; this.attempt = 0; this.view.status = 'Connected'; this.view.snapshot = message.snapshot; this.emit();
      if (this.pending?.request) {this.socket!.send(this.pending.request); this.deadline();}
      else this.flush();
      void this.call('subscribe', {afterEvent: message.snapshot.eventSequence}).catch(error => this.report(error));
      return;
    }
    if (message.type === 'notification') {this.events(message.payload); return;}
    if (!this.pending || message.requestId !== 'request-'+this.pending.sequence || message.sequence !== this.pending.sequence?.toString()) throw new Error('Unexpected engine reply');
    clearTimeout(this.replyTimer);
    const pending = this.pending; this.pending = null;
    this.queuedBytes -= pending.cost;
    if (message.consumed && pending.sequence! >= this.next) this.next = pending.sequence!+1n;
    if (message.status === 'rejected') pending.reject(new Error(message.error?.code+': '+message.error?.message));
    else {
      if (pending.operation === 'snapshot') this.view.snapshot = message.payload as Snapshot;
      if (pending.operation === 'subscribe') this.events(message.payload as Events);
      pending.resolve(message);
    }
    this.emit(); this.flush();
  }
  private events(events: Events) {
    if (events.snapshot) {this.view.snapshot = events.snapshot; this.emit();}
    else if (events.events.length && !this.refreshing) {
      this.refreshing = true;
      void this.call('snapshot', {}).catch(error => this.report(error)).finally(() => {this.refreshing = false;});
    }
  }
  private flush() {
    if (!this.ready || this.pending || !this.hello || !this.queue.length) return;
    const pending = this.queue.shift()!; this.pending = pending; pending.sequence = this.next;
    pending.request = JSON.stringify({protocolVersion: 1, serviceId: this.hello.serviceId, clientId: this.clientId,
      sessionId: this.sessionId, requestId: 'request-'+this.next, sequence: this.next.toString(),
      expectedRevision: pending.revision, applyAt: null, operation: pending.operation, payload: JSON.parse(pending.payloadJson)});
    if (new TextEncoder().encode(pending.request).length > 1048576) {
      this.pending = null; this.queuedBytes -= pending.cost; pending.reject(new Error('Request exceeds the 1 MiB transport limit')); this.emit(); this.flush(); return;
    }
    this.socket!.send(pending.request); this.deadline(); this.emit();
  }
  call<K extends keyof Payloads>(operation: K, payload: Payloads[K], revision: Revision | null = null): Promise<Reply> {
    if (this.stopped || this.queue.length >= 32) return Promise.reject(new Error('Control queue unavailable or full'));
    let payloadJson: string;
    try {payloadJson = JSON.stringify(payload);} catch {return Promise.reject(new Error('Control payload is not serializable'));}
    const cost = new TextEncoder().encode(payloadJson).length;
    if (cost > 1046528 || this.queuedBytes+cost > 4194304) return Promise.reject(new Error('Control byte capacity exceeded'));
    this.queuedBytes += cost;
    return new Promise((resolve, reject) => {this.queue.push({operation, payloadJson, cost, revision, resolve, reject}); this.flush(); this.emit();});
  }
  async write(write: ParameterWrite) {
    const revision = this.view.snapshot?.authoredRevision;
    if (revision === undefined) throw new Error('Engine snapshot unavailable');
    await this.call('parameters', {writes: [{schemaVersion: 1, write}]}, revision);
    await this.call('snapshot', {});
  }
  report(error: unknown) {this.view.error = error instanceof Error ? error.message : String(error); this.emit();}
  reconnect() {this.socket?.close();}
  private deadline() {
    clearTimeout(this.replyTimer);
    this.replyTimer = setTimeout(() => {this.view.error = 'Engine reply timed out; reconnecting with the original request.'; this.socket?.close(); this.emit();}, 10000);
  }
  private abandon(reason: string) {
    if (this.pending) this.pending.reject(new Error(reason));
    for (const pending of this.queue) pending.reject(new Error(reason));
    this.pending = null; this.queue = []; this.queuedBytes = 0; this.view.error = reason;
  }
  stop() {this.stopped = true; clearTimeout(this.timer); clearTimeout(this.replyTimer); this.socket?.close(); this.abandon('Editor disconnected');}
}
