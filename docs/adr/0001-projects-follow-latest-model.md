# Saved projects follow the latest piano model

A project stores Forte's control values, not a frozen copy of the engine or piano model. Opening an old project in a newer Forte therefore plays it with the improved model, and it may sound slightly different. The alternative was to ship every past model and engine version inside the plugin so old projects stay bit-identical. We rejected it because Forte is a free, one-person project and expects frequent sound improvements: carrying and testing every old version forever would cost more than occasional small changes in old projects.

## Consequences

- Every control is defined relative to the real piano ("50 = the real piano"), so saved values keep their meaning when the model is refitted.
- Release notes must call out any change to the sound.
- Automatable parameter IDs never change, and state formats load forwards (unknown fields are ignored) and backwards (newer Forte opens older states).
