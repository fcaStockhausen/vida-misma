# The Director: Player as Environment Architect {#sec:director}

The Director is the human intervention role. It is distinct from the autonomous
institutional policy and from the configuration enum `DirectorMode`. The player
changes environmental and institutional parameters; inhabitants continue to
choose their own actions through the utility pipeline.

## Typed Intervention Boundary

**Implemented.** `DirectorCommand` is a closed variant with exactly five command
types:[^director-api]

| Command | Accepted parameters | Effect and constraints |
|---|---|---|
| `DirectorSetQuota` | nonnegative finite output per tick | fixes subsequent demand; explicitly rejected in `CALM` |
| `DirectorSetZone` | grid coordinate and occupancy capacity $0\ldots8$ | applies only to `Floor` or `OpenSpace`; this is anonymous physical capacity, not a profession or cultural zone |
| `DirectorPlaceStructure` | coordinate, structure type, and machine subtype or conveyor direction where relevant | places a completed Wall, Storage, Food/Materials/Output Machine, or Conveyor on a compatible physical site |
| `DirectorRemoveStructure` | coordinate | removes Wall, Storage, Machine, or Conveyor; Entrance and Exit are protected |
| `DirectorSetMaintenancePriority` | built conveyor coordinate and `Normal` or `High` | changes an observable priority signal but does not repair the belt or select a worker |

FoodMachine placement requires a FoodSource, MaterialsMachine placement requires
a ScrapPile, and OutputMachine, Wall, Storage, and Conveyor placement require
Floor. Removing a source-backed machine restores the underlying resource deposit.
Removing storage or a loaded conveyor records discarded contents as physical
loss.

**Implemented invariant.** No Director command accepts an agent identity, action,
behavioral target, personality, opinion, relationship, community, or utility.
Static tests inspect both the public type boundary and its implementation for
those forbidden dependencies. The Director can make an action more attractive or
feasible by changing the world, but cannot make an inhabitant perform it.

## Indirection

**Design objective.** Player influence should remain environmental. A maintenance
priority can raise the utility of repairing a visible belt, a storage placement
can shorten a physical transfer, and a quota can alter institutional demand. None
of these interventions identifies who must respond or guarantees that anyone
will.

This is narrower than the construction and designation interfaces described in
older drafts. There is no implemented command for assigning a workshop,
residential district, artistic quarter, job, schedule, or individual order.
`DirectorSetZone` means only sustained occupancy capacity, and canonical closure
reads occupants at a coordinate without consulting their identities or culture.

## Player View and Debug View

**Implemented.** The GUI starts in a player-facing view. Its panel exposes
observable institutional and environmental consequences such as demand and fill,
stocks, output flow, infrastructure condition, density, occupancy zones,
maintenance priority, and factual events. Pressing `E` opens the five Director
tools and pauses the simulation while editing.[^director-gui]

Exact needs, personality facets, directed relationships, and per-action utility
are debug information rather than assumed player knowledge. `F12` explicitly
toggles the debug view; `--debug` may also start the GUI there. This separation is
epistemic as well as visual: internal values can be inspected for model diagnosis
without being presented as information on which ordinary Director play is based.

## Recording and Replay

**Implemented.** Every accepted intervention is recorded at the simulation tick
before `Simulation::advance()` and receives a strictly increasing global sequence
number. Invalid commands are atomic and do not enter the ledger.

`vida_gui --seed N --record FILE` writes a TOML log with format
`vida-interventions`, schema version 2, seed, Director mode,
`tick_phase = "before_advance"`, and an FNV-1a fingerprint of the loaded
configuration source. `vida_batch replay <ticks> <seed> <file>` rejects an
incompatible header, seed, configuration fingerprint, ordering, tick, command, or
parameter, and applies accepted events at their exact recorded phase.[^director-replay]

The round-trip test compares the original session and replay across the Director
ledger, metrics, full grid state, population, quota, and Chronicle. The CLI test
also requires byte-identical same-build replay output and rejects a mismatched seed
or configuration source. Replay is therefore a current verification mechanism,
not a future design proposal.

## Scope of Claims

**Implemented.** The Director is a human environmental editor; the canonical
factory policy is indifferent software. Neither is a RimWorld-style Storyteller
that selects narrative events from inhabitant psychology. Chronicle stores
factual events, while narrative rendering is behavior-neutral.

**Not established.** The available commands make controlled intervention and
replay possible, but no experiment yet establishes that a particular Director
strategy causes specialization, community formation, spatial segregation, or
long-run survival. Those are outcome hypotheses and require comparative runs,
not conclusions from the API design.

[^director-api]: `src/director.h` defines the five-command variant;
    `src/sim_director.cpp` validates and applies it. The boundary audit is in
    `tests/verify_policy_audit.cmake`.
[^director-gui]: See `src/main_gui.cpp`, `src/graphical_view.h`, and
    `src/graphical_view.cpp`.
[^director-replay]: Serialization is in `src/director.cpp`; batch replay is in
    `src/batch_main.cpp`. `test_director_environmental_commands` and
    `test_director_log_round_trip_and_replay` in `tests/simulation_tests.cpp`,
    together with `tests/verify_replay.cmake`, cover the contract.
