# Runtime Integration, Lifecycle, And Multiplayer

Use this reference when an effect is spawned, attached, parameterized, pooled, replicated, or driven by gameplay code. Gameplay code owns truth; Niagara and material components render that truth.

## Integration Boundary

- The server owns damage, hit confirmation, projectile state, cooldowns, target selection, and persistent outcomes.
- Clients own transient rendering, including particles, trails, camera-local post process, and cosmetic sound unless the design states otherwise.
- Replicate compact semantic information: effect type, transform, target/endpoints, seed, start time, team/color variant, and terminal reason.
- Do not replicate individual particles, Niagara internal state, or a stream of cosmetic transforms when clients can reconstruct the same presentation.
- A dedicated server should not create cosmetic Niagara components or material-only actors.

For GuLiStrike missiles, follow the existing client visual-subsystem pattern: authoritative missile state remains server-side and clients reconstruct/update the visual representation. Handle correction, late join, and terminal events explicitly.

## Blueprint Integration

For a one-shot world effect:

1. Trigger from an authoritative result or replicated cosmetic event.
2. On a rendering client, spawn the Niagara System at the confirmed transform.
3. Set all required User parameters immediately.
4. Use auto-destroy or a pool only when the System's lifetime behavior matches it.
5. Store the component reference if it must be corrected, stopped, or faded later.

For an attached effect, define attachment target, socket, location/rotation rule, scale inheritance, and what happens when the owner is destroyed. Stop/deactivate the component before destroying or recycling its owner if the effect needs a graceful tail.

For material-only effects, create a Dynamic Material Instance once, cache it, initialize parameters before reveal, animate a stable parameter contract, then release the actor/component or return it to a pool after fade completion.

## C++ Integration

The GuLiStrike game module already includes the Niagara dependency. Check the current `Build.cs` before adding or changing dependencies.

A typical cosmetic one-shot looks like:

```cpp
if (GetNetMode() != NM_DedicatedServer && ImpactSystem)
{
    UNiagaraFunctionLibrary::SpawnSystemAtLocation(
        GetWorld(), ImpactSystem, ImpactLocation, ImpactRotation);
}
```

For an attached or controllable effect, retain the returned `UNiagaraComponent*`, set User parameters, and define ownership/lifetime explicitly:

```cpp
UNiagaraComponent* Component = UNiagaraFunctionLibrary::SpawnSystemAttached(
    TrailSystem, AttachComponent, SocketName,
    FVector::ZeroVector, FRotator::ZeroRotator,
    EAttachLocation::SnapToTarget, true, true,
    ENCPoolMethod::AutoRelease, true);

if (Component)
{
    Component->SetVariableLinearColor(TEXT("User.Tint"), Tint);
}
```

Treat the snippets as patterns, not drop-in API proof: confirm the UE5.7 overload and pooling semantics in the local engine headers. Use soft references when asset loading policy requires them, and ensure assets are cooked through a stable reference or explicit asset-management rule.

## Lifecycle Rules

- One-shot: spawn, allow completion, auto-destroy or auto-release only after the System naturally completes.
- Looping attached effect: keep a component reference, deactivate on state exit, optionally allow particles to finish, then release.
- Correctable effect: update a small set of stable User parameters or the owning component transform; avoid reconstructing every frame without need.
- Interrupted effect: distinguish immediate kill from graceful deactivation according to the gameplay cue.
- Owner destruction: decide whether the effect follows, detaches and finishes, or stops immediately.

Ensure emitters can actually complete. Infinite loops, immortal particles, or constantly reset age will defeat auto-destroy and pooling.

## Pooling

Use pooling for frequent, short-lived effects after validating reset behavior:

- reset all User parameters and transforms on acquire;
- ensure previous particles and renderer state do not leak into reuse;
- choose auto-release or manual-release ownership consistently;
- test rapid reacquire, interrupted effects, owner destruction, and map transition;
- measure whether pooling improves the actual spawn workload before increasing complexity.

Pooling does not fix expensive per-frame simulation or translucent overdraw; profile those separately.

## Multiplayer Patterns

- Reliable gameplay outcome, unreliable cosmetic event: reconstruct from replicated authoritative state when loss would leave a misleading persistent visual.
- One-shot impact/explosion: multicast or client-observed state change can spawn a local effect; avoid a replicated VFX Actor unless its lifetime/state genuinely requires it.
- Long-lived trail/beam: clients attach a local component to the replicated gameplay object and update compact endpoints or state.
- Prediction: predicted cosmetic feedback must reconcile cleanly with rejection or authoritative correction.
- Late join: rebuild only persistent/long-lived presentation from current state; do not replay expired bursts.

Test listen server, remote client, and dedicated server separately. Confirm the dedicated server produces no cosmetic components while clients still receive enough information to render the effect.

## Project-Specific Material Effects

`ABlinkVFX` and its afterimage/heatwave materials are valid material-based effects. Continue using their mesh/material lifecycle when the request is about the existing blink look. Introduce Niagara only for a requested particle layer, spatial burst, trail, or orchestration need; do not replace the proven path by default.

