#include "Battle/Network/GuLiIpConnection.h"
#include "Engine/NetDriver.h"
#include "EngineLogs.h"
#include "HAL/IConsoleManager.h"

namespace
{
	TAutoConsoleVariable<int32> CVarRealTimeBudget(TEXT("guli.Net.RealTimeBudget"), 1,
		TEXT("Reconcile GameNetDriver connection credit using elapsed network time; 0 retains engine accounting."));
	TAutoConsoleVariable<int32> CVarBudgetDiagnostics(TEXT("guli.Net.BudgetDiagnostics"), 0,
		TEXT("Log connection refill interval, sent bytes and debt once per second."));
}

void UGuLiIpConnection::Tick(float DeltaSeconds)
{
	const double PreviousTick = LastTickTime;
	const int32 PreviousDebt = QueuedBits;
	const uint32 PreviousBytes = static_cast<uint32>(OutTotalBytes);
	Super::Tick(DeltaSeconds);

	// IpConnection and NetConnection can both return before completing a tick.
	// LastTickTime only advances after the base connection accepts the interval.
	bool ThrottlingDisabled = false;
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	ThrottlingDisabled = NetConnectionHelper::HasDisabledBandwidthThrottling();
#endif
	if (!Driver || Driver->NetDriverName != NAME_GameNetDriver || IsReplay() || IsInternalAck()
		|| NetworkCongestionControl.IsSet() || ThrottlingDisabled
		|| GetConnectionState() != USOCK_Open || CurrentNetSpeed <= 0)
	{
		FractionalCreditBits = 0;
		return;
	}
	if (LastTickTime <= PreviousTick || !FMath::IsFinite(LastTickTime)) return;
	if (CVarRealTimeBudget.GetValueOnGameThread() == 0 || PreviousTick <= 0)
	{
		FractionalCreditBits = 0;
		return;
	}
	// Unsigned subtraction handles the int32 byte counter wrapping. A reset or
	// implausibly large jump is not interpreted as bytes actually sent this tick.
	const uint32 SentBytes = static_cast<uint32>(OutTotalBytes) - PreviousBytes;
	if (SentBytes > static_cast<uint32>(MAX_int32)) { FractionalCreditBits = 0; return; }
	const double Elapsed = FMath::Clamp(LastTickTime - PreviousTick, 0.0, 0.100);
	const double Credit = CurrentNetSpeed * 8.0 * Elapsed + FractionalCreditBits;
	const int64 WholeCredit = static_cast<int64>(Credit);
	FractionalCreditBits = Credit - WholeCredit;
	const int64 IdleLimit = static_cast<int64>(CurrentNetSpeed * 8.0 * 0.100);
	const int64 Debt = static_cast<int64>(PreviousDebt) + static_cast<int64>(SentBytes) * 8 - WholeCredit;
	QueuedBits = static_cast<int32>(FMath::Clamp<int64>(Debt, -IdleLimit, MAX_int32));
	if (Debt < -IdleLimit) FractionalCreditBits = 0;
	if (CVarBudgetDiagnostics.GetValueOnGameThread() && LastTickTime >= NextDiagnosticTime)
	{
		UE_LOG(LogNet, Log, TEXT("GuLiBudget connection=%s elapsed=%.6f refill=%.6f rate=%d sent=%u before=%d after=%d credit=%lld"),
			*GetName(), LastTickTime - PreviousTick, Elapsed, CurrentNetSpeed, SentBytes, PreviousDebt, QueuedBits, WholeCredit);
		NextDiagnosticTime = LastTickTime + 1.0;
	}
}
