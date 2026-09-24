#include "Commander/Orders/GuLiUnitTaskTypes.h"

bool FGuLiUnitTaskCommand::IsWellFormed() const
{
	if (!CommandId || !SelectionRevision || uint8(Disposition) > uint8(EGuLiTaskDisposition::Stop)
		|| uint8(Kind) > uint8(EGuLiUnitTaskKind::Transit) || Target.ContainsNaN()) return false;
	if (Disposition == EGuLiTaskDisposition::Stop) return true;
	if (Kind == EGuLiUnitTaskKind::Special) return SpecialTaskId > 0;
	return Kind != EGuLiUnitTaskKind::Transit || !TerritoryId.IsNone();
}
