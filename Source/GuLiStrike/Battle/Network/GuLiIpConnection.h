#pragma once

#include "CoreMinimal.h"
#include "IpConnection.h"
#include "GuLiIpConnection.generated.h"

/** Game transport credit follows the driver's clock, independently of render cadence. */
UCLASS(Transient, Config=Engine)
class GULISTRIKE_API UGuLiIpConnection : public UIpConnection
{
	GENERATED_BODY()
public:
	virtual void Tick(float DeltaSeconds) override;
private:
	double FractionalCreditBits = 0;
	double NextDiagnosticTime = 0;
};
