#pragma once

#include "Commander/Network/GuLiCommanderTypes.h"
#include "Commander/UI/GuLiSceneUITypes.h"

class AGuLiCommanderPresentationActor;
class APlayerController;
class UGuLiCommanderNetSyncComponent;
class FGuLiHealthBarPoseCache;
struct FGuLiHealthBarEvent;

namespace GuLiHealthBars
{
	FGuLiSceneUIHealthBar FromEvent(const UObject* Source, uint64 Element, const FGuLiHealthBarEvent& Event, double Now);
}

/** Local selection only styles bars already admitted by a vital change. */
struct FGuLiWorldHealthBars
{
	void Gather(APlayerController& Viewer, const AGuLiCommanderPresentationActor* Presentation,
		FGuLiHealthBarPoseCache& Poses, TArray<FGuLiSceneUIHealthBar>& Bars);

private:
	TWeakObjectPtr<const UGuLiCommanderNetSyncComponent> SelectionSource;
	uint32 SelectionRevision = 0;
	TSet<FGuLiSoldierId> SelectedSoldiers;
};
