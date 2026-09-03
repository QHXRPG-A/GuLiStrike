// Copyright Epic Games, Inc. All Rights Reserved.

#include "Battle/Combat/GuLiMissileVisualSubsystem.h"

#include "Engine/World.h"

namespace
{
	bool IsFiniteTime(const float Value)
	{
		return FMath::IsFinite(Value) && Value >= 0.0f;
	}
}

bool FGuLiMissileVisualLaunchDTO::IsWellFormed() const
{
	return MatchEpoch != 0u && MissileId.IsValid() && Emitter.IsValid() && Target.IsValid()
		&& !Position.ContainsNaN() && !Velocity.ContainsNaN() && !Velocity.IsNearlyZero()
		&& IsFiniteTime(ServerWorldTimeSeconds);
}

bool FGuLiMissileVisualCorrectionDTO::IsWellFormed() const
{
	return MatchEpoch != 0u && MissileId.IsValid() && SimulationSequence != 0u
		&& !Position.ContainsNaN() && !Velocity.ContainsNaN() && !Velocity.IsNearlyZero()
		&& IsFiniteTime(ServerWorldTimeSeconds);
}

bool FGuLiMissileVisualTerminalDTO::IsWellFormed() const
{
	return MatchEpoch != 0u && MissileId.IsValid() && SimulationSequence != 0u
		&& !Location.ContainsNaN() && IsFiniteTime(ServerWorldTimeSeconds);
}

bool UGuLiMissileVisualSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer) && World && World->IsGameWorld()
		&& World->GetNetMode() != NM_DedicatedServer;
}

void UGuLiMissileVisualSubsystem::Deinitialize()
{
	ActiveVisuals.Reset();
	TerminalSequences.Reset();
	TerminalOrder.Reset();
	MatchEpoch = 0u;
	OnVisualLaunch.Clear();
	OnVisualCorrection.Clear();
	OnVisualTerminal.Clear();
	OnVisualLaunchNative.Clear();
	OnVisualCorrectionNative.Clear();
	OnVisualTerminalNative.Clear();
	Super::Deinitialize();
}

void UGuLiMissileVisualSubsystem::BeginEpoch(const uint32 NewMatchEpoch)
{
	if (MatchEpoch == NewMatchEpoch)
	{
		return;
	}
	ActiveVisuals.Reset();
	TerminalSequences.Reset();
	TerminalOrder.Reset();
	MatchEpoch = NewMatchEpoch;
}

bool UGuLiMissileVisualSubsystem::ApplyLaunch(const FGuLiMissileVisualLaunchDTO& Event)
{
	if (!Event.IsWellFormed() || Event.MatchEpoch != MatchEpoch
		|| TerminalSequences.Contains(Event.MissileId) || ActiveVisuals.Contains(Event.MissileId))
	{
		return false;
	}
	FGuLiMissileVisualState& State = ActiveVisuals.Add(Event.MissileId);
	State.MatchEpoch = Event.MatchEpoch;
	State.MissileId = Event.MissileId;
	State.Emitter = Event.Emitter;
	State.Target = Event.Target;
	State.Position = Event.Position;
	State.Velocity = Event.Velocity;
	State.LastServerWorldTimeSeconds = Event.ServerWorldTimeSeconds;
	OnVisualLaunch.Broadcast(Event);
	OnVisualLaunchNative.Broadcast(Event);
	return true;
}

bool UGuLiMissileVisualSubsystem::ApplyCorrection(const FGuLiMissileVisualCorrectionDTO& Event)
{
	FGuLiMissileVisualState* State = ActiveVisuals.Find(Event.MissileId);
	if (!Event.IsWellFormed() || Event.MatchEpoch != MatchEpoch
		|| TerminalSequences.Contains(Event.MissileId) || !State
		|| Event.SimulationSequence <= State->SimulationSequence)
	{
		return false;
	}
	State->Position = Event.Position;
	State->Velocity = Event.Velocity;
	State->SimulationSequence = Event.SimulationSequence;
	State->LastServerWorldTimeSeconds = Event.ServerWorldTimeSeconds;
	OnVisualCorrection.Broadcast(Event);
	OnVisualCorrectionNative.Broadcast(Event);
	return true;
}

bool UGuLiMissileVisualSubsystem::ApplyTerminal(const FGuLiMissileVisualTerminalDTO& Event)
{
	if (!Event.IsWellFormed() || Event.MatchEpoch != MatchEpoch
		|| TerminalSequences.Contains(Event.MissileId))
	{
		return false;
	}
	ActiveVisuals.Remove(Event.MissileId);
	RememberTerminal(Event.MissileId, Event.SimulationSequence);
	OnVisualTerminal.Broadcast(Event);
	OnVisualTerminalNative.Broadcast(Event);
	return true;
}

bool UGuLiMissileVisualSubsystem::TryGetVisualState(
	const FGuid& MissileId,
	FGuLiMissileVisualState& OutState) const
{
	OutState = FGuLiMissileVisualState{};
	if (const FGuLiMissileVisualState* State = ActiveVisuals.Find(MissileId))
	{
		OutState = *State;
		return true;
	}
	return false;
}

void UGuLiMissileVisualSubsystem::RememberTerminal(
	const FGuid& MissileId,
	const uint32 Sequence)
{
	TerminalSequences.Add(MissileId, Sequence);
	TerminalOrder.Add(MissileId);
	const int32 Overflow = TerminalOrder.Num() - FMath::Max(1, MaximumRememberedTerminals);
	if (Overflow <= 0)
	{
		return;
	}
	for (int32 Index = 0; Index < Overflow; ++Index)
	{
		TerminalSequences.Remove(TerminalOrder[Index]);
	}
	TerminalOrder.RemoveAt(0, Overflow, EAllowShrinking::No);
}

