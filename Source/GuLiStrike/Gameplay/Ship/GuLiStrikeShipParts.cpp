// Copyright Epic Games, Inc. All Rights Reserved.

#include "GuLiStrikeShipParts.h"

UGuLiStrikeShipPartComponent::UGuLiStrikeShipPartComponent()
{
	// parts are pure visuals: the hull capsule provides the ship collision
	SetCollisionProfileName(FName("NoCollision"));
	SetGenerateOverlapEvents(false);
}

bool UGuLiStrikeShipPartComponent::CanAttachToSocket(FName SocketName) const
{
	return CompatibleSockets.Contains(SocketName);
}
