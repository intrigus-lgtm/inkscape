/*
 *  ShapeMisc.cpp
 *  nlivarot
 *
 *  Created by fred on Sun Jul 20 2003.
 *
 */

#include "Shape.h"
#include "Path.h"
#include "../libnr/nr-types.h"
//#include "MyMath.h"

// until i find something better
#define MiscNormalize(v) {\
  double _l=sqrt(dot(v,v)); \
    if ( _l < 0.0000001 ) { \
      v.pt[0]=v.pt[1]=0; \
    } else { \
      v/=_l; \
    }\
}

void
Shape::ConvertToForme (Path * dest)
{
  if (nbPt <= 1 || nbAr <= 1)
    return;
  if (Eulerian (true) == false)
    return;
  
  dest->Reset ();
  dest->SetWeighted (false);
  
  MakePointData (true);
  MakeEdgeData (true);
  MakeSweepDestData (true);
  
  for (int i = 0; i < nbPt; i++)
  {
    pData[i].rx.pt[0] = Round (pts[i].x.pt[0]);
    pData[i].rx.pt[1] = Round (pts[i].x.pt[1]);
  }
  for (int i = 0; i < nbAr; i++)
  {
    eData[i].rdx = pData[aretes[i].en].rx - pData[aretes[i].st].rx;
  }
  
  SortEdges ();
  
  for (int i = 0; i < nbAr; i++)
  {
    swdData[i].misc = 0;
    swdData[i].precParc = swdData[i].suivParc = -1;
  }
  
  int searchInd = 0;
  
  int lastPtUsed = 0;
  do
  {
    int startBord = -1;
    {
      int fi = 0;
      for (fi = lastPtUsed; fi < nbPt; fi++)
      {
        if (pts[fi].firstA >= 0 && swdData[pts[fi].firstA].misc == 0)
          break;
      }
      lastPtUsed = fi + 1;
      if (fi < nbPt)
      {
        int bestB = pts[fi].firstA;
        while (bestB >= 0 && aretes[bestB].st != fi)
          bestB = NextAt (fi, bestB);
        if (bestB >= 0)
	      {
          startBord = bestB;
          dest->MoveTo (pts[aretes[startBord].en].x);
	      }
      }
    }
    if (startBord >= 0)
    {
      // parcours en profondeur pour mettre les leF et riF a leurs valeurs
      swdData[startBord].misc = (void *) 1;
      //                      printf("part de %d\n",startBord);
      int curBord = startBord;
      bool back = false;
      swdData[curBord].precParc = -1;
      swdData[curBord].suivParc = -1;
      do
	    {
	      int cPt = aretes[curBord].en;
	      int nb = curBord;
        //                              printf("de curBord= %d au point %i  -> ",curBord,cPt);
	      do
        {
          int nnb = CycleNextAt (cPt, nb);
          if (nnb == nb)
          {
            // cul-de-sac
            nb = -1;
            break;
          }
          nb = nnb;
          if (nb < 0 || nb == curBord)
            break;
        }
	      while (swdData[nb].misc != 0 || aretes[nb].st != cPt);
        
	      if (nb < 0 || nb == curBord)
        {
          if (back == false)
            dest->Close ();
          back = true;
          // retour en arriere
          curBord = swdData[curBord].precParc;
          //                                      printf("retour vers %d\n",curBord);
          if (curBord < 0)
            break;
        }
	      else
        {
          if (back)
          {
            dest->MoveTo (pts[cPt].x);
            back = false;
          }
          swdData[nb].misc = (void *) 1;
          swdData[nb].ind = searchInd++;
          swdData[nb].precParc = curBord;
          swdData[curBord].suivParc = nb;
          curBord = nb;
          //                                      printf("suite %d\n",curBord);
          {
            dest->LineTo (pts[aretes[nb].en].x);
          }
        }
	    }
      while (1 /*swdData[curBord].precParc >= 0 */ );
      // fin du cas non-oriente
    }
  }
  while (lastPtUsed < nbPt);
  
  MakePointData (false);
  MakeEdgeData (false);
  MakeSweepDestData (false);
}

void
Shape::ConvertToForme (Path * dest, int nbP, Path * *orig)
{
  if (nbPt <= 1 || nbAr <= 1)
    return;
  if (Eulerian (true) == false)
    return;
  
  if (HasBackData () == false)
  {
    ConvertToForme (dest);
    return;
  }
  
  dest->Reset ();
  dest->SetWeighted (false);
  
  MakePointData (true);
  MakeEdgeData (true);
  MakeSweepDestData (true);
  
  for (int i = 0; i < nbPt; i++)
  {
    pData[i].rx.pt[0] = Round (pts[i].x.pt[0]);
    pData[i].rx.pt[1] = Round (pts[i].x.pt[1]);
  }
  for (int i = 0; i < nbAr; i++)
  {
    eData[i].rdx = pData[aretes[i].en].rx - pData[aretes[i].st].rx;
  }
  
  SortEdges ();
  
  for (int i = 0; i < nbAr; i++)
  {
    swdData[i].misc = 0;
    swdData[i].precParc = swdData[i].suivParc = -1;
  }
  
  int searchInd = 0;
  
  int lastPtUsed = 0;
  do
  {
    int startBord = -1;
    {
      int fi = 0;
      for (fi = lastPtUsed; fi < nbPt; fi++)
      {
        if (pts[fi].firstA >= 0 && swdData[pts[fi].firstA].misc == 0)
          break;
      }
      lastPtUsed = fi + 1;
      if (fi < nbPt)
      {
        int bestB = pts[fi].firstA;
        while (bestB >= 0 && aretes[bestB].st != fi)
          bestB = NextAt (fi, bestB);
        if (bestB >= 0)
	      {
          startBord = bestB;
          //                                      dest->MoveTo(pts[aretes[startBord].en].x,pts[aretes[startBord].en].y);
	      }
      }
    }
    if (startBord >= 0)
    {
      // parcours en profondeur pour mettre les leF et riF a leurs valeurs
      swdData[startBord].misc = (void *) 1;
      //                      printf("part de %d\n",startBord);
      int curBord = startBord;
      bool back = false;
      swdData[curBord].precParc = -1;
      swdData[curBord].suivParc = -1;
      do
	    {
	      int cPt = aretes[curBord].en;
	      int nb = curBord;
	      //                              printf("de curBord= %d au point %i  -> ",curBord,cPt);
	      do
        {
          int nnb = CycleNextAt (cPt, nb);
          if (nnb == nb)
          {
            // cul-de-sac
            nb = -1;
            break;
          }
          nb = nnb;
          if (nb < 0 || nb == curBord)
            break;
        }
	      while (swdData[nb].misc != 0 || aretes[nb].st != cPt);
        
	      if (nb < 0 || nb == curBord)
        {
          if (back == false)
          {
            if (curBord == startBord || curBord < 0)
            {
              // probleme -> on vire le moveto
              //                                                      dest->descr_nb--;
            }
            else
            {
              swdData[curBord].suivParc = -1;
              AddContour (dest, nbP, orig, startBord, curBord);
            }
            //                                              dest->Close();
          }
          back = true;
          // retour en arriere
          curBord = swdData[curBord].precParc;
          //                                      printf("retour vers %d\n",curBord);
          if (curBord < 0)
            break;
        }
	      else
        {
          if (back)
          {
            //                                              dest->MoveTo(pts[cPt].x,pts[cPt].y);
            back = false;
            startBord = nb;
          }
          swdData[nb].misc = (void *) 1;
          swdData[nb].ind = searchInd++;
          swdData[nb].precParc = curBord;
          swdData[curBord].suivParc = nb;
          curBord = nb;
          //                                      printf("suite %d\n",curBord);
          //                                      dest->LineTo(pts[aretes[nb].en].x,pts[aretes[nb].en].y);
        }
	    }
      while (1 /*swdData[curBord].precParc >= 0 */ );
      // fin du cas non-oriente
    }
  }
  while (lastPtUsed < nbPt);
  
  MakePointData (false);
  MakeEdgeData (false);
  MakeSweepDestData (false);
}

// offsets
int
Shape::MakeOffset (Shape * a, float dec, JoinType join, float miter)
{
  Reset (0, 0);
  if ( a->HasBackData() ) MakeBackData(true); else MakeBackData (false);
  if (dec == 0)
  {
    nbPt = a->nbPt;
    if (nbPt > maxPt)
    {
      maxPt = nbPt;
      pts = (dg_point *) realloc (pts, maxPt * sizeof (dg_point));
      if (HasPointsData ())
        pData =
          (point_data *) realloc (pData, maxPt * sizeof (point_data));
    }
    memcpy (pts, a->pts, nbPt * sizeof (dg_point));
    
    nbAr = a->nbAr;
    if (nbAr > maxAr)
    {
      maxAr = nbAr;
      aretes = (dg_arete *) realloc (aretes, maxAr * sizeof (dg_arete));
      if (HasEdgesData ())
        eData = (edge_data *) realloc (eData, maxPt * sizeof (edge_data));
      if (HasSweepSrcData ())
        swsData =
          (sweep_src_data *) realloc (swsData,
                                      maxAr * sizeof (sweep_src_data));
      if (HasSweepDestData ())
        swdData =
          (sweep_dest_data *) realloc (swdData,
                                       maxAr * sizeof (sweep_dest_data));
      if (HasRasterData ())
        swrData =
          (raster_data *) realloc (swrData, maxAr * sizeof (raster_data));
      if (HasBackData ())
        ebData =
          (back_data *) realloc (ebData, maxAr * sizeof (back_data));
    }
    memcpy (aretes, a->aretes, nbAr * sizeof (dg_arete));
    return 0;
  }
  if (a->nbPt <= 1 || a->nbAr <= 1 || a->type != shape_polygon)
    return shape_input_err;
  
  a->SortEdges ();
  
  a->MakeSweepDestData (true);
  a->MakeSweepSrcData (true);
  
  for (int i = 0; i < a->nbAr; i++)
  {
    //              int    stP=a->swsData[i].stPt/*,enP=a->swsData[i].enPt*/;
    int stB = -1, enB = -1;
    if (dec > 0)
    {
      stB = a->CycleNextAt (a->aretes[i].st, i);
      enB = a->CyclePrevAt (a->aretes[i].en, i);
    }
    else
    {
      stB = a->CyclePrevAt (a->aretes[i].st, i);
      enB = a->CycleNextAt (a->aretes[i].en, i);
    }
    
    NR::Point stD, seD, enD;
    float stL, seL, enL;
    stD = a->aretes[stB].dx;
    seD = a->aretes[i].dx;
    enD = a->aretes[enB].dx;
     
    stL = sqrt (dot(stD,stD));
    seL = sqrt (dot(seD,seD));
    enL = sqrt (dot(enD,enD));
    MiscNormalize (stD);
    MiscNormalize (enD);
    MiscNormalize (seD);
    
    NR::Point ptP;
    int stNo, enNo;
    ptP = a->pts[a->aretes[i].st].x;
    int   usePathID=-1;
    int   usePieceID=0;
    float useT=0.0;
    if ( a->HasBackData() ) {
      if ( a->ebData[i].pathID >= 0 && a->ebData[stB].pathID == a->ebData[i].pathID && a->ebData[stB].pieceID == a->ebData[i].pieceID
           && a->ebData[stB].tEn == a->ebData[i].tSt ) {
        usePathID=a->ebData[i].pathID;
        usePieceID=a->ebData[i].pieceID;
        useT=a->ebData[i].tSt;
      }
    }
    if (dec > 0)
    {
      Path::DoRightJoin (this, dec, join, ptP, stD, seD, miter, stL, seL,
                         stNo, enNo,usePathID,usePieceID,useT);
      a->swsData[i].stPt = enNo;
      a->swsData[stB].enPt = stNo;
    }
    else
    {
     Path::DoLeftJoin (this, -dec, join, ptP, stD, seD, miter, stL, seL,
                        stNo, enNo,usePathID,usePieceID,useT);
      a->swsData[i].stPt = enNo;
      a->swsData[stB].enPt = stNo;
    }
  }
  if (dec < 0)
  {
    for (int i = 0; i < nbAr; i++)
      Inverse (i);
  }
  if ( HasBackData() ) {
    for (int i = 0; i < a->nbAr; i++)
    {
      int nEd=AddEdge (a->swsData[i].stPt, a->swsData[i].enPt);
      ebData[nEd]=a->ebData[i];
    }
  } else {
    for (int i = 0; i < a->nbAr; i++)
    {
      AddEdge (a->swsData[i].stPt, a->swsData[i].enPt);
    }
  }
  a->MakeSweepSrcData (false);
  a->MakeSweepDestData (false);
  
  return 0;
}

void
Shape::AddContour (Path * dest, int nbP, Path * *orig, int startBord,
                   int curBord)
{
  int bord = startBord;
  
  {
    dest->MoveTo (pts[aretes[bord].st].x);
  }
  
  while (bord >= 0)
  {
    int nPiece = ebData[bord].pieceID;
    int nPath = ebData[bord].pathID;
    
    if (nPath < 0 || nPath >= nbP || orig[nPath] == NULL)
    {
      // segment batard
      dest->LineTo (pts[aretes[bord].en].x);
      bord = swdData[bord].suivParc;
    }
    else
    {
      Path *from = orig[nPath];
      if (nPiece < 0 || nPiece >= from->descr_nb)
	    {
	      // segment batard
	      dest->LineTo (pts[aretes[bord].en].x);
	      bord = swdData[bord].suivParc;
	    }
      else
	    {
	      int nType = from->descr_data[nPiece].flags & descr_type_mask;
	      if (nType == descr_close || nType == descr_moveto
            || nType == descr_forced)
        {
          // devrait pas arriver
          dest->LineTo (pts[aretes[bord].en].x);
          bord = swdData[bord].suivParc;
        }
	      else if (nType == descr_lineto)
        {
          bord = ReFormeLineTo (bord, curBord, dest, from);
        }
	      else if (nType == descr_arcto)
        {
          bord = ReFormeArcTo (bord, curBord, dest, from);
        }
	      else if (nType == descr_cubicto)
        {
          bord = ReFormeCubicTo (bord, curBord, dest, from);
        }
	      else if (nType == descr_bezierto)
        {
          if (from->descr_data[nPiece].d.b.nb == 0)
          {
            bord = ReFormeLineTo (bord, curBord, dest, from);
          }
          else
          {
            bord = ReFormeBezierTo (bord, curBord, dest, from);
          }
        }
	      else if (nType == descr_interm_bezier)
        {
          bord = ReFormeBezierTo (bord, curBord, dest, from);
        }
	      else
        {
          // devrait pas arriver non plus
          dest->LineTo (pts[aretes[bord].en].x);
          bord = swdData[bord].suivParc;
        }
	      if (bord >= 0
            && pts[aretes[bord].st].dI + pts[aretes[bord].st].dO > 2 ) {
          dest->ForcePoint ();
        } else if ( bord >= 0 && pts[aretes[bord].st].oldDegree > 2 && pts[aretes[bord].st].dI + pts[aretes[bord].st].dO == 2)  {
          if ( HasBackData() ) {
            int   prevEdge=pts[aretes[bord].st].firstA;
            int   nextEdge=pts[aretes[bord].st].lastA;
            if ( aretes[prevEdge].en != aretes[bord].st ) {
              int  swai=prevEdge;prevEdge=nextEdge;nextEdge=swai;
            }
            if ( ebData[prevEdge].pieceID == ebData[nextEdge].pieceID  && ebData[prevEdge].pathID == ebData[nextEdge].pathID ) {
              if ( fabsf(ebData[prevEdge].tEn-ebData[nextEdge].tSt) < 0.05 ) {
              } else {
                dest->ForcePoint ();
              }
            } else {
              dest->ForcePoint ();
            }
          } else {
            dest->ForcePoint ();
          }    
        }
      }
    }
  }
  dest->Close ();
}

int
Shape::ReFormeLineTo (int bord, int curBord, Path * dest, Path * orig)
{
  int nPiece = ebData[bord].pieceID;
  int nPath = ebData[bord].pathID;
  float /*ts=ebData[bord].tSt, */ te = ebData[bord].tEn;
  NR::Point nx = pts[aretes[bord].en].x;
  bord = swdData[bord].suivParc;
  while (bord >= 0)
  {
    if (pts[aretes[bord].st].dI + pts[aretes[bord].st].dO > 2
        || pts[aretes[bord].st].oldDegree > 2)
    {
      break;
    }
    if (ebData[bord].pieceID == nPiece && ebData[bord].pathID == nPath)
    {
      if (fabs (te - ebData[bord].tSt) > 0.0001)
        break;
      nx = pts[aretes[bord].en].x;
       te = ebData[bord].tEn;
    }
    else
    {
      break;
    }
    bord = swdData[bord].suivParc;
  }
  {
    dest->LineTo (nx);
  }
  return bord;
}

int
Shape::ReFormeArcTo (int bord, int curBord, Path * dest, Path * from)
{
  int nPiece = ebData[bord].pieceID;
  int nPath = ebData[bord].pathID;
  float ts = ebData[bord].tSt, te = ebData[bord].tEn;
  //      float  px=pts[aretes[bord].st].x,py=pts[aretes[bord].st].y;
  NR::Point nx = pts[aretes[bord].en].x;
  bord = swdData[bord].suivParc;
  while (bord >= 0)
  {
    if (pts[aretes[bord].st].dI + pts[aretes[bord].st].dO > 2
        || pts[aretes[bord].st].oldDegree > 2)
    {
      break;
    }
    if (ebData[bord].pieceID == nPiece && ebData[bord].pathID == nPath)
    {
      if (fabs (te - ebData[bord].tSt) > 0.0001)
	    {
	      break;
	    }
      nx = pts[aretes[bord].en].x;
      te = ebData[bord].tEn;
    }
    else
    {
      break;
    }
    bord = swdData[bord].suivParc;
  }
  float sang, eang;
  bool nLarge = from->descr_data[nPiece].d.a.large;
  bool nClockwise = from->descr_data[nPiece].d.a.clockwise;
  NR::Point prevx;
  from->PrevPoint (nPiece - 1, prevx);
  Path::ArcAngles (prevx, from->descr_data[nPiece].d.a.p,
                   from->descr_data[nPiece].d.a.rx,
                   from->descr_data[nPiece].d.a.ry,
                   from->descr_data[nPiece].d.a.angle, nLarge, nClockwise,
                   sang, eang);
  if (nClockwise)
  {
    if (sang < eang)
      sang += 2 * M_PI;
  }
  else
  {
    if (sang > eang)
      sang -= 2 * M_PI;
  }
  float delta = eang - sang;
  float ndelta = delta * (te - ts);
  if (ts > te)
    nClockwise = !nClockwise;
  if (ndelta < 0)
    ndelta = -ndelta;
  if (ndelta > M_PI)
    nLarge = true;
  else
    nLarge = false;
  /*	if ( delta < 0 ) delta=-delta;
	if ( ndelta < 0 ) ndelta=-ndelta;
	if ( ( delta < M_PI && ndelta < M_PI ) || ( delta >= M_PI && ndelta >= M_PI ) ) {
		if ( ts < te ) {
		} else {
			nClockwise=!(nClockwise);
		}
	} else {
    //		nLarge=!(nLarge);
		nLarge=false; // c'est un sous-segment -> l'arc ne peut que etre plus petit
		if ( ts < te ) {
		} else {
			nClockwise=!(nClockwise);
		}
	}*/
  {
    dest->ArcTo (nx, from->descr_data[nPiece].d.a.rx,
                 from->descr_data[nPiece].d.a.ry,
                 from->descr_data[nPiece].d.a.angle, nLarge, nClockwise);
  }
  return bord;
}

int
Shape::ReFormeCubicTo (int bord, int curBord, Path * dest, Path * from)
{
  int nPiece = ebData[bord].pieceID;
  int nPath = ebData[bord].pathID;
  float ts = ebData[bord].tSt, te = ebData[bord].tEn;
  NR::Point nx = pts[aretes[bord].en].x;
  bord = swdData[bord].suivParc;
  while (bord >= 0)
  {
    if (pts[aretes[bord].st].dI + pts[aretes[bord].st].dO > 2
        || pts[aretes[bord].st].oldDegree > 2)
    {
      break;
    }
    if (ebData[bord].pieceID == nPiece && ebData[bord].pathID == nPath)
    {
      if (fabs (te - ebData[bord].tSt) > 0.0001)
	    {
	      break;
	    }
      nx = pts[aretes[bord].en].x;
      te = ebData[bord].tEn;
    }
    else
    {
      break;
    }
    bord = swdData[bord].suivParc;
  }
  NR::Point prevx;
  from->PrevPoint (nPiece - 1, prevx);
  
  NR::Point sDx, eDx;
  Path::CubicTangent (ts, sDx, prevx,
                      from->descr_data[nPiece].d.c.stD,
                      from->descr_data[nPiece].d.c.p,
                      from->descr_data[nPiece].d.c.enD);
  Path::CubicTangent (te, eDx, prevx,
                      from->descr_data[nPiece].d.c.stD,
                      from->descr_data[nPiece].d.c.p,
                      from->descr_data[nPiece].d.c.enD);
  sDx *= (te - ts);
  eDx *= (te - ts);
  {
    dest->CubicTo (nx,sDx,eDx);
  }
  return bord;
}

int
Shape::ReFormeBezierTo (int bord, int curBord, Path * dest, Path * from)
{
  int nPiece = ebData[bord].pieceID;
  int nPath = ebData[bord].pathID;
  float ts = ebData[bord].tSt, te = ebData[bord].tEn;
  int ps = nPiece, pe = nPiece;
  NR::Point px = pts[aretes[bord].st].x;
  NR::Point nx = pts[aretes[bord].en].x;
  int inBezier = -1, nbInterm = -1;
  int typ;
  typ = from->descr_data[nPiece].flags & descr_type_mask;
  if (typ == descr_bezierto)
  {
    inBezier = nPiece;
    nbInterm = from->descr_data[nPiece].d.b.nb;
  }
  else
  {
    int n = nPiece - 1;
    while (n > 0)
    {
      typ = from->descr_data[n].flags & descr_type_mask;
      if (typ == descr_bezierto)
      {
        inBezier = n;
        nbInterm = from->descr_data[n].d.b.nb;
        break;
      }
      n--;
    }
    if (inBezier < 0)
    {
      bord = swdData[bord].suivParc;
      dest->LineTo (nx);
      return bord;
    }
  }
  bord = swdData[bord].suivParc;
  while (bord >= 0)
  {
    if (pts[aretes[bord].st].dI + pts[aretes[bord].st].dO > 2
        || pts[aretes[bord].st].oldDegree > 2)
    {
      break;
    }
    if (ebData[bord].pathID == nPath)
    {
      if (ebData[bord].pieceID < inBezier
          || ebData[bord].pieceID >= inBezier + nbInterm)
        break;
      if (ebData[bord].pieceID == pe
          && fabs (te - ebData[bord].tSt) > 0.0001)
        break;
      if (ebData[bord].pieceID != pe
          && (ebData[bord].tSt > 0.0001 && ebData[bord].tSt < 0.9999))
        break;
      if (ebData[bord].pieceID != pe && (te > 0.0001 && te < 0.9999))
        break;
      nx = pts[aretes[bord].en].x;
      te = ebData[bord].tEn;
      pe = ebData[bord].pieceID;
    }
    else
    {
      break;
    }
    bord = swdData[bord].suivParc;
  }
  
  NR::Point bstx;
  NR::Point benx;
  from->PrevPoint (inBezier - 1, bstx);
  benx = from->descr_data[inBezier].d.b.p;
  
  if (pe == ps)
  {
    ReFormeBezierChunk (px, nx, dest, inBezier, nbInterm, from, ps,
                        ts, te);
  }
  else if (ps < pe)
  {
    if (ts < 0.0001)
    {
      if (te > 0.9999)
      {
        dest->BezierTo (nx);
        for (int i = ps; i <= pe; i++)
        {
          dest->IntermBezierTo (from->descr_data[i + 1].d.i.p);
        }
        dest->EndBezierTo ();
      }
      else
      {
        NR::Point tx;
        tx =
          (from->descr_data[pe + 1].d.i.p +
           from->descr_data[pe].d.i.p) / 2;
        dest->BezierTo (tx);
        for (int i = ps; i < pe; i++)
        {
          dest->IntermBezierTo (from->descr_data[i + 1].d.i.p);
        }
        dest->EndBezierTo ();
        ReFormeBezierChunk (tx, nx, dest, inBezier, nbInterm,
                            from, pe, 0.0, te);
      }
    }
    else
    {
      if (te > 0.9999)
      {
        NR::Point tx;
        tx =
          (from->descr_data[ps + 1].d.i.p +
           from->descr_data[ps + 2].d.i.p) / 2;
        ReFormeBezierChunk (px, tx, dest, inBezier, nbInterm,
                            from, ps, ts, 1.0);
        dest->BezierTo (nx);
        for (int i = ps + 1; i <= pe; i++)
        {
          dest->IntermBezierTo (from->descr_data[i + 1].d.i.p);
        }
        dest->EndBezierTo ();
      }
      else
      {
        NR::Point tx;
        tx =
          (from->descr_data[ps + 1].d.i.p +
           from->descr_data[ps + 2].d.i.p) / 2;
        ReFormeBezierChunk (px, tx, dest, inBezier, nbInterm,
                            from, ps, ts, 1.0);
        tx =
          (from->descr_data[pe + 1].d.i.p +
           from->descr_data[pe].d.i.p )/ 2;
        dest->BezierTo (tx);
        for (int i = ps + 1; i <= pe; i++)
        {
          dest->IntermBezierTo (from->descr_data[i + 1].d.i.p);
        }
        dest->EndBezierTo ();
        ReFormeBezierChunk (tx, nx, dest, inBezier, nbInterm,
                            from, pe, 0.0, te);
      }
    }
  }
  else
  {
    if (ts > 0.9999)
    {
      if (te < 0.0001)
      {
        dest->BezierTo (nx);
        for (int i = ps; i >= pe; i--)
        {
          dest->IntermBezierTo (from->descr_data[i + 1].d.i.p);
        }
        dest->EndBezierTo ();
      }
      else
      {
        NR::Point tx;
        tx =
          (from->descr_data[pe + 1].d.i.p +
           from->descr_data[pe + 2].d.i.p) / 2;
        dest->BezierTo (tx);
        for (int i = ps; i > pe; i--)
        {
          dest->IntermBezierTo (from->descr_data[i + 1].d.i.p);
        }
        dest->EndBezierTo ();
        ReFormeBezierChunk (tx, nx, dest, inBezier, nbInterm,
                            from, pe, 1.0, te);
      }
    }
    else
    {
      if (te < 0.0001)
      {
        NR::Point tx;
        tx =
          (from->descr_data[ps + 1].d.i.p +
           from->descr_data[ps].d.i.p) / 2;
        ReFormeBezierChunk (px, tx, dest, inBezier, nbInterm,
                            from, ps, ts, 0.0);
        dest->BezierTo (nx);
        for (int i = ps + 1; i >= pe; i--)
        {
          dest->IntermBezierTo (from->descr_data[i].d.i.p);
        }
        dest->EndBezierTo ();
      }
      else
      {
        NR::Point tx;
        tx =
          (from->descr_data[ps + 1].d.i.p +
           from->descr_data[ps].d.i.p) / 2;
        ReFormeBezierChunk (px, tx, dest, inBezier, nbInterm,
                            from, ps, ts, 0.0);
        tx =
          (from->descr_data[pe + 1].d.i.p +
           from->descr_data[pe + 2].d.i.p) / 2;
        dest->BezierTo (tx);
        for (int i = ps + 1; i > pe; i--)
        {
          dest->IntermBezierTo (from->descr_data[i].d.i.p);
        }
        dest->EndBezierTo ();
        ReFormeBezierChunk (tx, nx, dest, inBezier, nbInterm,
                            from, pe, 1.0, te);
      }
    }
  }
  return bord;
}

void
Shape::ReFormeBezierChunk (NR::Point px, NR::Point nx,
                           Path * dest, int inBezier, int nbInterm,
                           Path * from, int p, float ts, float te)
{
  NR::Point bstx;
  NR::Point benx;
  from->PrevPoint (inBezier - 1, bstx);
  benx = from->descr_data[inBezier].d.b.p;
  
  NR::Point mx;
  if (p == inBezier)
  {
    // premier bout
    if (nbInterm <= 1)
    {
      // seul bout de la spline
      mx = from->descr_data[inBezier + 1].d.i.p;
    }
    else
    {
      // premier bout d'une spline qui en contient plusieurs
      mx = from->descr_data[inBezier + 1].d.i.p;
      benx = (from->descr_data[inBezier + 2].d.i.p + mx) / 2;
    }
  }
  else if (p == inBezier + nbInterm - 1)
  {
    // dernier bout
    // si nbInterm == 1, le cas a deja ete traite
    // donc dernier bout d'une spline qui en contient plusieurs
    mx = from->descr_data[inBezier + nbInterm].d.i.p;
    bstx = (from->descr_data[inBezier + nbInterm - 1].d.i.p + mx) / 2;
  }
  else
  {
    // la spline contient forcément plusieurs bouts, et ce n'est ni le premier ni le dernier
    mx = from->descr_data[p + 1].d.i.p;
    bstx = (from->descr_data[p].d.i.p + mx) / 2;
    benx = (from->descr_data[p + 2].d.i.p + mx) / 2;
  }
  NR::Point cx;
  {
    Path::QuadraticPoint ((ts + te) / 2, cx, bstx, mx, benx);
  }
  cx = 2 * cx - (px + nx) / 2;
  {
    dest->BezierTo (nx);
    dest->IntermBezierTo (cx);
    dest->EndBezierTo ();
  }
}

#undef MiscNormalize
