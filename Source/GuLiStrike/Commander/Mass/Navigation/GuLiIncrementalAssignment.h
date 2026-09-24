#pragma once
#include "CoreMinimal.h"

/** Resumable rectangular Hungarian assignment. One Step scans at most one column row. */
struct FGuLiIncrementalAssignment
{
	TArray<FVector> Rows, Columns;
	TArray<double> U,V,Minimum;
	TArray<int32> RowByColumn,Previous,ColumnByRow;
	TArray<uint8> Used;
	int32 Row=1, Current=0;
	bool bAugmenting=false, bComplete=false;
	void Begin(TArray<FVector>&& InRows,TArray<FVector>&& InColumns)
	{
		*this=FGuLiIncrementalAssignment{}; Rows=MoveTemp(InRows); Columns=MoveTemp(InColumns);
		check(Rows.Num()<=Columns.Num()); U.Init(0.,Rows.Num()+1); V.Init(0.,Columns.Num()+1);
		RowByColumn.Init(0,Columns.Num()+1); Previous.Init(0,Columns.Num()+1); ColumnByRow.Init(INDEX_NONE,Rows.Num());
		bComplete=Rows.IsEmpty();
	}
	void Step()
	{
		if (bComplete) return;
		if (!bAugmenting)
		{
			RowByColumn[0]=Row; Current=0; Minimum.Init(TNumericLimits<double>::Max(),Columns.Num()+1);
			Used.Init(0,Columns.Num()+1); bAugmenting=true;
		}
		Used[Current]=1; const int32 CurrentRow=RowByColumn[Current];
		double Delta=TNumericLimits<double>::Max(); int32 Next=0;
		for (int32 C=1; C<=Columns.Num(); ++C) if (!Used[C])
		{
			const double Reduced=FVector::DistSquared2D(Rows[CurrentRow-1],Columns[C-1])-U[CurrentRow]-V[C];
			if (Reduced<Minimum[C]) { Minimum[C]=Reduced; Previous[C]=Current; }
			if (Minimum[C]<Delta) { Delta=Minimum[C]; Next=C; }
		}
		check(Next>0 && FMath::IsFinite(Delta));
		for (int32 C=0; C<=Columns.Num(); ++C)
			if (Used[C]) { U[RowByColumn[C]]+=Delta; V[C]-=Delta; } else Minimum[C]-=Delta;
		Current=Next;
		if (RowByColumn[Current]) return;
		do { const int32 Prior=Previous[Current]; RowByColumn[Current]=RowByColumn[Prior]; Current=Prior; } while (Current);
		bAugmenting=false;
		if (++Row>Rows.Num())
		{
			for (int32 C=1; C<=Columns.Num(); ++C) if (RowByColumn[C]) ColumnByRow[RowByColumn[C]-1]=C-1;
			bComplete=true;
		}
	}
};
