/* <lalVerbatim file="LALInspiralSetSearchLimitsCV">
Author: Churches, D. K.
$Id$
</lalVerbatim>  */

/*  <lalLaTeX>

\subsection{Module \texttt{LALInspiralSetSearchLimits.c}}

Function which calculates the minimum and maximum values of $\tau_{0}$ and $\tau_{3}$.
\subsubsection*{Prototypes}
\vspace{0.1in}
\input{LALInspiralSetSearchLimitsCP}
\idx{LALInspiralSetSearchLimits()}
\begin{itemize}
   \item \texttt{bankParams,} Output containing the boundary of search, current lattice point, etc.
   \item \texttt{coarseIn,} Input, specifies the parameters of the search space.
\end{itemize}

This Function calculates the minimum and maximum values of $\tau_{0}$ and $\tau_{3}$
as determined by the total mass of the binary $m$ and the symmetric 
mass ratio $\eta$.  The function also calulates the coordinates of the
first template in the bank. These coordinates are $\tau_{0}=\tau_{0min}$,
$\tau_{3}=\tau_{3min}$. 

\subsubsection*{Description}

We start with the definition of the chirp times $\tau_{0}$ and $\tau_{3}$,
\begin{equation}
\tau_{0} = \frac{5}{256 (\pi f_{a} )^{8/3} m^{5/3} \eta}
\end{equation}

and

\begin{equation}
\tau_{3} = \frac{1}{8 (\pi^{2} f_{a}^{5} )^{1/3} m^{2/3} \eta}
\end{equation}

$\tau_{0}$ is minimised when $\eta=1/4$ and $\mathtt{m=MMax}$.
$\tau_{0}$ is maximised when $\eta=1/4$ and $\mathtt{m=2mMin}$.
$\tau_{3}$ is minimised when $\eta=1/4$ and $\mathtt{m=MMax}$.
$\tau_{3}$ is maximised when
$\eta=\mathtt{ mMin(MMax-mMin)/MMax^{2} }$.


\subsubsection*{Algorithm}


\subsubsection*{Uses}

\subsubsection*{Notes}

\vfill{\footnotesize\input{LALInspiralSetSearchLimitsCV}}

</lalLaTeX>  */



#include <lal/LALInspiralBank.h>
#include <lal/LALStdlib.h>


NRCSID (LALINSPIRALSETSEARCHLIMITSC, "$Id$");

/*  <lalVerbatim file="LALInspiralSetSearchLimitsCP">  */
void
LALInspiralSetSearchLimits (
    LALStatus            *status,
    InspiralBankParams   *bankParams,
    InspiralCoarseBankIn  coarseIn
    )
/* </lalVerbatim> */
{  
   InspiralTemplate *Pars1=NULL, *Pars2=NULL, *Pars3=NULL, *Pars4=NULL;

   INITSTATUS( status, "LALInspiralSetSearchLimits", 
       LALINSPIRALSETSEARCHLIMITSC );
   ATTATCHSTATUSPTR( status );

   ASSERT( bankParams, status, 
       LALINSPIRALBANKH_ENULL, LALINSPIRALBANKH_MSGENULL );
   ASSERT( coarseIn.space == Tau0Tau2 || coarseIn.space == Tau0Tau3, status,
       LALINSPIRALBANKH_ECHOICE, LALINSPIRALBANKH_MSGECHOICE );
   ASSERT( coarseIn.mMin > 0, status, 
       LALINSPIRALBANKH_ESIZE, LALINSPIRALBANKH_MSGESIZE );
   ASSERT( coarseIn.MMax >= 2. * coarseIn.mMin, status, 
       LALINSPIRALBANKH_ESIZE, LALINSPIRALBANKH_MSGESIZE );
   ASSERT( coarseIn.mmCoarse > 0., status, 
       LALINSPIRALBANKH_ESIZE, LALINSPIRALBANKH_MSGESIZE );
   ASSERT( coarseIn.mmCoarse < 1., status, 
       LALINSPIRALBANKH_ESIZE, LALINSPIRALBANKH_MSGESIZE );
   ASSERT( coarseIn.fLower > 0., status, 
       LALINSPIRALBANKH_ESIZE, LALINSPIRALBANKH_MSGESIZE );
   ASSERT( coarseIn.tSampling > 0., status, 
       LALINSPIRALBANKH_ESIZE, LALINSPIRALBANKH_MSGESIZE );

   Pars1 = (InspiralTemplate *) LALCalloc( 1, sizeof(InspiralTemplate) );
   Pars2 = (InspiralTemplate *) LALCalloc( 1, sizeof(InspiralTemplate) );
   Pars3 = (InspiralTemplate *) LALCalloc( 1, sizeof(InspiralTemplate) );
   Pars4 = (InspiralTemplate *) LALCalloc( 1, sizeof(InspiralTemplate) );

   if ( ! Pars1 || ! Pars2 || ! Pars3 || !Pars4 )
   {
     ABORT( status, LALINSPIRALBANKH_EMEM, LALINSPIRALBANKH_MSGEMEM );
   }

   /* Initiate three parameter vectors consistent with the coarseIn structure */
   LALInspiralSetParams(status->statusPtr, Pars1, coarseIn);
   CHECKSTATUSPTR(status);
   LALInspiralSetParams(status->statusPtr, Pars2, coarseIn);
   CHECKSTATUSPTR(status);
   LALInspiralSetParams(status->statusPtr, Pars3, coarseIn);
   CHECKSTATUSPTR(status);
   LALInspiralSetParams(status->statusPtr, Pars4, coarseIn);
   CHECKSTATUSPTR(status);

   Pars1->massChoice = Pars2->massChoice = Pars3->massChoice = m1Andm2;
   Pars4->massChoice = m1Andm2;

   /* Calculate the value of the parameters at the three corners */
   /* of the search space                                        */
   Pars1->mass1 = Pars1->mass2 = coarseIn.MMax/2.;
   LALInspiralParameterCalc( status->statusPtr, Pars1 );
   CHECKSTATUSPTR( status );

   if ( coarseIn.massRange == MinMaxComponentTotalMass )
   {
     Pars2->mass1 = Pars2->mass2 = coarseIn.MMin/2.;
   }
   else
   {   
     Pars2->mass1 = Pars2->mass2 = coarseIn.mMin;
   }
   LALInspiralParameterCalc( status->statusPtr, Pars2 );
   CHECKSTATUSPTR( status );
   
   Pars3->mass1 = coarseIn.mMin;
   Pars3->mass2 = coarseIn.MMax - coarseIn.mMin;
   LALInspiralParameterCalc( status->statusPtr, Pars3 ); 
   CHECKSTATUSPTR( status );

   if ( coarseIn.massRange == MinMaxComponentTotalMass )
   {
     Pars4->mass1 = coarseIn.mMin;
     Pars4->mass2 = coarseIn.MMin - coarseIn.mMin;
     LALInspiralParameterCalc( status->statusPtr, Pars4 );
     CHECKSTATUSPTR( status );
   }
   else
   {
     Pars4->t0 = 0.0;
   }

   /* Find the minimum and maximum values of the parameters and set     */
   /* the search space.  (The minimum values of chirp times are those   */
   /* corresponding to m1 = m2 = MMax/2, i.e., Pars1 structure.         */
   bankParams->x0 = bankParams->x0Min = Pars1->t0;
   bankParams->x0Max = (Pars2->t0 > Pars4->t0) ? Pars2->t0 : Pars4->t0;

   switch ( coarseIn.space ) 
   {
     case Tau0Tau2:
       bankParams->x1 = bankParams->x1Min = Pars1->t2;
       bankParams->x1Max = (Pars2->t2 > Pars3->t2) ? Pars2->t2 : Pars3->t2;
       break;
       
     case Tau0Tau3:
       bankParams->x1 = bankParams->x1Min = Pars1->t3;
       bankParams->x1Max = (Pars2->t3 > Pars3->t3) ? Pars2->t3 : Pars3->t3;
       break;
   
     default:
       ABORT( status, LALINSPIRALBANKH_ECHOICE, LALINSPIRALBANKH_MSGECHOICE );
   }
   
   LALFree( Pars1 );
   LALFree( Pars2 );
   LALFree( Pars3 );
   LALFree( Pars4 );

   DETATCHSTATUSPTR( status );
   RETURN( status );
}
