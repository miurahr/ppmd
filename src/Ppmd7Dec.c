/* Ppmd7Dec.c -- PPMdH Decoder
2017-04-03 : Igor Pavlov : Public domain
This code is based on PPMd var.H (2001): Dmitry Shkarin : Public domain */

#include "Compiler.h"

#include "Ppmd7.h"

#define kTopValue (1 << 24)

Bool Ppmd7z_RangeDec_Init(CPpmd7z_RangeDec *p)
{
  unsigned i;
  p->Code = 0;
  p->Range = 0xFFFFFFFF;
  if (IByteIn_Read(p->Stream) != 0)
    return False;
  for (i = 0; i < 4; i++)
    p->Code = (p->Code << 8) | IByteIn_Read(p->Stream);
  return (p->Code < 0xFFFFFFFF);
}

static UInt32 Range_GetThreshold(CPpmd7z_RangeDec *p, UInt32 total)
{
  return p->Code / (p->Range /= total);
}

static void Range_Normalize(CPpmd7z_RangeDec *p)
{
  if (p->Range < kTopValue)
  {
    p->Code = (p->Code << 8) | IByteIn_Read(p->Stream);
    p->Range <<= 8;
    if (p->Range < kTopValue)
    {
      p->Code = (p->Code << 8) | IByteIn_Read(p->Stream);
      p->Range <<= 8;
    }
  }
}

static void Range_Decode(CPpmd7z_RangeDec *p, UInt32 start, UInt32 size)
{
  p->Code -= start * p->Range;
  p->Range *= size;
  Range_Normalize(p);
}

static UInt32 Range_DecodeBit(CPpmd7z_RangeDec *p, UInt32 size0)
{
  UInt32 newBound = (p->Range >> 14) * size0;
  UInt32 symbol;
  if (p->Code < newBound)
  {
    symbol = 0;
    p->Range = newBound;
  }
  else
  {
    symbol = 1;
    p->Code -= newBound;
    p->Range -= newBound;
  }
  Range_Normalize(p);
  return symbol;
}

#define MASK(sym) ((signed char *)charMask)[sym]

int Ppmd7_DecodeSymbol(CPpmd7 *p, CPpmd7z_RangeDec *rc)
{
  size_t charMask[256 / sizeof(size_t)];
  if (p->MinContext->NumStats != 1)
  {
    CPpmd_State *s = Ppmd7_GetStats(p, p->MinContext);
    unsigned i;
    UInt32 count, hiCnt;
    if ((count = Range_GetThreshold(rc, p->MinContext->SummFreq)) < (hiCnt = s->Freq))
    {
      Byte symbol;
      Range_Decode(rc, 0, s->Freq);
      p->FoundState = s;
      symbol = s->Symbol;
      Ppmd7_Update1_0(p);
      return symbol;
    }
    p->PrevSuccess = 0;
    i = p->MinContext->NumStats - 1;
    do
    {
      if ((hiCnt += (++s)->Freq) > count)
      {
        Byte symbol;
        Range_Decode(rc, hiCnt - s->Freq, s->Freq);
        p->FoundState = s;
        symbol = s->Symbol;
        Ppmd7_Update1(p);
        return symbol;
      }
    }
    while (--i);
    if (count >= p->MinContext->SummFreq)
      return -2;
    p->HiBitsFlag = p->HB2Flag[p->FoundState->Symbol];
    Range_Decode(rc, hiCnt, p->MinContext->SummFreq - hiCnt);
    PPMD_SetAllBitsIn256Bytes(charMask);
    MASK(s->Symbol) = 0;
    i = p->MinContext->NumStats - 1;
    do { MASK((--s)->Symbol) = 0; } while (--i);
  }
  else
  {
    UInt16 *prob = Ppmd7_GetBinSumm(p);
    if (Range_DecodeBit(rc, *prob) == 0)
    {
      Byte symbol;
      *prob = (UInt16)PPMD_UPDATE_PROB_0(*prob);
      symbol = (p->FoundState = Ppmd7Context_OneState(p->MinContext))->Symbol;
      Ppmd7_UpdateBin(p);
      return symbol;
    }
    *prob = (UInt16)PPMD_UPDATE_PROB_1(*prob);
    p->InitEsc = PPMD7_kExpEscape[*prob >> 10];
    PPMD_SetAllBitsIn256Bytes(charMask);
    MASK(Ppmd7Context_OneState(p->MinContext)->Symbol) = 0;
    p->PrevSuccess = 0;
  }
  for (;;)
  {
    CPpmd_State *ps[256], *s;
    UInt32 freqSum, count, hiCnt;
    CPpmd_See *see;
    unsigned i, num, numMasked = p->MinContext->NumStats;
    do
    {
      p->OrderFall++;
      if (!p->MinContext->Suffix)
        return -1;
      p->MinContext = Ppmd7_GetContext(p, p->MinContext->Suffix);
    }
    while (p->MinContext->NumStats == numMasked);
    hiCnt = 0;
    s = Ppmd7_GetStats(p, p->MinContext);
    i = 0;
    num = p->MinContext->NumStats - numMasked;
    do
    {
      int k = (int)(MASK(s->Symbol));
      hiCnt += (s->Freq & k);
      ps[i] = s++;
      i -= k;
    }
    while (i != num);
    
    see = Ppmd7_MakeEscFreq(p, numMasked, &freqSum);
    freqSum += hiCnt;
    count = Range_GetThreshold(rc, freqSum);
    
    if (count < hiCnt)
    {
      Byte symbol;
      CPpmd_State **pps = ps;
      for (hiCnt = 0; (hiCnt += (*pps)->Freq) <= count; pps++);
      s = *pps;
      Range_Decode(rc, hiCnt - s->Freq, s->Freq);
      Ppmd_See_Update(see);
      p->FoundState = s;
      symbol = s->Symbol;
      Ppmd7_Update2(p);
      return symbol;
    }
    if (count >= freqSum)
      return -2;
    Range_Decode(rc, hiCnt, freqSum - hiCnt);
    see->Summ = (UInt16)(see->Summ + freqSum);
    do { MASK(ps[--i]->Symbol) = 0; } while (i != 0);
  }
}
