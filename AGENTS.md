# MapGyver 2

- Use the [MapGyver implementation plan](docs/plan/MapGyver_Implementation_Plan.md) and [status](docs/progress/mapgyver-status.md) to continue product work.
- Keep media composition, Matrix, screen policy, and specialized editors in this product. Use Golden's public runtime, parameter, and UI APIs. Shared code and generated contracts belong in Golden Core.
- Update the `golden/` pin only to a published, verified commit. Test ordinary authoring through the product's existing startup and automation entry points. Qualify installers and native hardware after the software checks pass.
- Run locked Cargo and npm checks with the pinned platform tools. Use `cargo fmt --all` for MapGyver and check Golden's formatting separately when changing the pin. Keep test code and fixtures under `tests/`.
