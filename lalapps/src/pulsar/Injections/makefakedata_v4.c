/*-----------------------------------------------------------------------
 *
 * File Name: makefakedata_v4.c
 *
 * Authors: R. Prix, M.A. Papa, X. Siemens, B. Allen, C. Messenger
 *
 * Revision: $Id$
 *
 *           
 *-----------------------------------------------------------------------
 */

/* ---------- includes ---------- */
#include <sys/stat.h>

#include <lalapps.h>
#include <lal/AVFactories.h>
#include <lal/SeqFactories.h>
#include <lal/LALInitBarycenter.h>
#include <lal/Random.h>
#include <gsl/gsl_math.h>

#include <lal/UserInput.h>
#include <lal/SFTfileIO.h>
#include <lal/GeneratePulsarSignal.h>


RCSID ("$Id$");

/* Error codes and messages */
/* <lalErrTable file="MAKEFAKEDATACErrorTable"> */
#define MAKEFAKEDATAC_ENORM 	0
#define MAKEFAKEDATAC_ESUB  	1
#define MAKEFAKEDATAC_EARG  	2
#define MAKEFAKEDATAC_EBAD  	3
#define MAKEFAKEDATAC_EFILE 	4
#define MAKEFAKEDATAC_ENOARG 	5
#define MAKEFAKEDATAC_EMEM 	6
#define MAKEFAKEDATAC_EBINARYOUT 7
#define MAKEFAKEDATAC_EREADFILE 8

#define MAKEFAKEDATAC_MSGENORM "Normal exit"
#define MAKEFAKEDATAC_MSGESUB  "Subroutine failed"
#define MAKEFAKEDATAC_MSGEARG  "Error parsing arguments"
#define MAKEFAKEDATAC_MSGEBAD  "Bad argument values"
#define MAKEFAKEDATAC_MSGEFILE "File IO error"
#define MAKEFAKEDATAC_MSGENOARG "Missing argument"
#define MAKEFAKEDATAC_MSGEMEM 	"Out of memory..."
#define MAKEFAKEDATAC_MSGEBINARYOUT "Error in writing binary-data to stdout"
#define MAKEFAKEDATAC_MSGEREADFILE "Error reading in file"
/* </lalErrTable> */

/***************************************************/
#define TRUE (1==1)
#define FALSE (1==0)

#define myMax(x,y) ( (x) > (y) ? (x) : (y) )
/*----------------------------------------------------------------------*/
/** configuration-variables derived from user-variables */
typedef struct 
{
  EphemerisData edat;		/**< ephemeris-data */
  LALDetector site;  		/**< detector-site info */

  LIGOTimeGPS startTimeGPS;	/**< start-time of observation */
  UINT4 duration;		/**< total duration of observation in seconds */

  LIGOTimeGPSVector *timestamps;/**< a vector of timestamps to generate time-series/SFTs for */

  LIGOTimeGPS refTime;		/**< reference-time for pulsar-parameters in SBB frame */
  REAL8 fmin_eff;		/**< 'effective' fmin: round down such that fmin*Tsft = integer! */
  REAL8 fBand_eff;		/**< 'effective' frequency-band such that samples/SFT is integer */
  REAL8Vector *spindown;	/**< vector of frequency-derivatives of GW signal */

  SFTVector *noiseSFTs;		/**< vector of noise-SFTs to be added to signal */
  REAL8 noiseSigma;		/**< sigma for Gaussian noise to be added */
  BOOLEAN binaryPulsar;		/**< are we dealing with a binary pulsar? */
  COMPLEX8FrequencySeries *transfer;  /**< detector's transfer function for use in hardware-injection */
} ConfigVars_t;

/** Default year-span of ephemeris-files to be used */
#define EPHEM_YEARS  "00-04"

/* ---------- local prototypes ---------- */
void FreeMem (LALStatus *, ConfigVars_t *cfg);
void InitUserVars (LALStatus *);
void InitMakefakedata (LALStatus *, ConfigVars_t *cfg, int argc, char *argv[]);
void AddGaussianNoise (LALStatus *, REAL4TimeSeries *outSeries, REAL4TimeSeries *inSeries, REAL4 sigma);
void GetOrbitalParams (LALStatus *, BinaryOrbitParams *orbit);
void WriteMFDlog (LALStatus *, char *argv[], const char *logfile);


void LoadTransferFunctionFromActuation(LALStatus *, 
				       COMPLEX8FrequencySeries **transfer, 
				       REAL8 actuationScale,
				       const CHAR *fname);

void LALExtractSFTBand ( LALStatus *, SFTVector **outSFTs, const SFTVector *inSFTs, REAL8 fmin, REAL8 Band );


extern void write_timeSeriesR4 (FILE *fp, const REAL4TimeSeries *series);
extern void write_timeSeriesR8 (FILE *fp, const REAL8TimeSeries *series);
BOOLEAN is_directory ( const CHAR *fname );
/*----------------------------------------------------------------------*/
static const PulsarSignalParams empty_params;
static const SFTParams empty_sftParams;
static const EphemerisData empty_edat;
static const ConfigVars_t empty_GV;
static const SFTConstraints empty_SFTConstraints;
/*----------------------------------------------------------------------*/
/* User variables */
/*----------------------------------------------------------------------*/
BOOLEAN uvar_help;		/**< Print this help/usage message */

/* output */
CHAR *uvar_outSFTbname;		/**< Path and basefilename of output SFT files */
BOOLEAN uvar_outSFTv1;		/**< use v1-spec for output-SFTs */

CHAR *uvar_TDDfile;		/**< Filename for ASCII output time-series */
BOOLEAN uvar_hardwareTDD;	/**< Binary output timeseries in chunks of Tsft for hardware injections. */

CHAR *uvar_logfile;		/**< name of logfile */

/* specify start + duration */
CHAR *uvar_timestampsFile;	/**< Timestamps file */
INT4 uvar_startTime;		/**< Start-time of requested signal in detector-frame (GPS seconds) */
INT4 uvar_duration;		/**< Duration of requested signal in seconds */

/* generation mode of timer-series: all-at-once or per-sft */
INT4 uvar_generationMode;	/**< How to generate the timeseries: all-at-once or per-sft */
typedef enum
  {
    GENERATE_ALL_AT_ONCE = 0,	/**< generate whole time-series at once before turning into SFTs */
    GENERATE_PER_SFT,		/**< generate timeseries individually for each SFT */
    GENERATE_LAST		/**< end-marker */
  } GenerationMode;

/* time-series sampling + heterodyning frequencies */
REAL8 uvar_fmin;		/**< Lowest frequency in output SFT (= heterodyning frequency) */
REAL8 uvar_Band;		/**< bandwidth of output SFT in Hz (= 1/2 sampling frequency) */

/* SFT params */
REAL8 uvar_Tsft;		/**< SFT time baseline Tsft */
REAL8 uvar_SFToverlap;		/**< overlap SFTs by this many seconds */

/* noise to add [OPTIONAL] */
CHAR *uvar_noiseSFTs;		/**< Glob-like pattern specifying noise-SFTs to be added to signal */
REAL8 uvar_noiseSigma;		/**< Gaussian noise with standard-deviation sigma */
REAL8 uvar_noiseSqrtSh;		/**< ALTERNATIVE: single-sided sqrt(Sh) for Gaussian noise */

/* Detector and ephemeris */
CHAR *uvar_detector;		/**< Detector: LHO, LLO, VIRGO, GEO, TAMA, CIT, ROME */
CHAR *uvar_actuation;		/**< filename containg detector actuation function */
REAL8 uvar_actuationScale;	/**< Scale-factor to be applied to actuation-function */
CHAR *uvar_ephemDir;		/**< Directory path for ephemeris files (optional), use LAL_DATA_PATH if unset. */
CHAR *uvar_ephemYear;		/**< Year (or range of years) of ephemeris files to be used */

/* pulsar parameters [REQUIRED] */
REAL8 uvar_refTime;		/**< Pulsar reference time tRef in SSB ('0' means: use startTime converted to SSB) */

REAL8 uvar_aPlus;		/**< Plus polarization amplitude aPlus */
REAL8 uvar_aCross;		/**< Cross polarization amplitude aCross */
REAL8 uvar_psi;			/**< Polarization angle psi */
REAL8 uvar_phi0;		/**< Initial phase phi */
REAL8 uvar_f0;			/**< Gravitational wave-frequency f0 at tRef */
REAL8 uvar_longitude;		/**< Right ascension [radians] alpha of pulsar */
REAL8 uvar_latitude;		/**< Declination [radians] delta of pulsar */
REAL8 uvar_f1dot;		/**< First spindown parameter f' */
REAL8 uvar_f2dot;		/**< Second spindown parameter f'' */
REAL8 uvar_f3dot;		/**< Third spindown parameter f''' */

/* orbital parameters [OPTIONAL] */

REAL8 uvar_orbitSemiMajorAxis;	/**< Projected orbital semi-major axis in seconds (a/c) */
REAL8 uvar_orbitEccentricity;	/**< Orbital eccentricity */
INT4  uvar_orbitTperiSSBsec;	/**< 'observed' (SSB) time of periapsis passage. Seconds. */
INT4  uvar_orbitTperiSSBns;	/**< 'observed' (SSB) time of periapsis passage. Nanoseconds. */
REAL8 uvar_orbitPeriod;		/**< Orbital period (seconds) */
REAL8 uvar_orbitArgPeriapse;	/**< Argument of periapsis (radians) */

/* precision-level of signal-generation */
BOOLEAN uvar_exactSignal;	/**< generate signal timeseries as exactly as possible (slow) */


/*----------------------------------------------------------------------*/

extern int vrbflg;

/*----------------------------------------------------------------------
 * main function 
 *----------------------------------------------------------------------*/
int
main(int argc, char *argv[]) 
{
  LALStatus status = blank_status;	/* initialize status */
  ConfigVars_t GV = empty_GV;
  PulsarSignalParams params = empty_params;
  SFTVector *SFTs = NULL;
  REAL4TimeSeries *Tseries = NULL;
  BinaryOrbitParams orbit;
  UINT4 i_chunk, numchunks;

  UINT4 i;

  lalDebugLevel = 0;	/* default value */
  vrbflg = 1;		/* verbose error-messages */

  /* set LAL error-handler */
  lal_errhandler = LAL_ERR_EXIT;	/* exit with returned status-code on error */

  /* ------------------------------ 
   * read user-input and set up shop
   *------------------------------*/
  LAL_CALL (LALGetDebugLevel(&status, argc, argv, 'v'), &status);

  LAL_CALL (InitMakefakedata(&status, &GV, argc, argv), &status);

  if (uvar_help)	/* help was called, do nothing here.. */
    return (0);

  /*----------------------------------------
   * fill the PulsarSignalParams struct 
   *----------------------------------------*/
  /* pulsar params */
  params.pulsar.tRef = GV.refTime;
  params.pulsar.position.longitude = uvar_longitude;
  params.pulsar.position.latitude  = uvar_latitude;
  params.pulsar.position.system    = COORDINATESYSTEM_EQUATORIAL;
  params.pulsar.psi 		   = uvar_psi;
  params.pulsar.aPlus		   = uvar_aPlus;
  params.pulsar.aCross		   = uvar_aCross;
  params.pulsar.phi0		   = uvar_phi0;
  params.pulsar.f0		   = uvar_f0;
  params.pulsar.spindown           = GV.spindown;

  /* orbital params */
  if (GV.binaryPulsar)
    {
      LAL_CALL ( GetOrbitalParams(&status, &orbit), &status);
      params.orbit = &orbit;
    }
  else
    params.orbit = NULL;

  /* detector params */
  params.transfer = GV.transfer;	/* detector transfer function (NULL if not used) */	
  params.site = &(GV.site);	
  params.ephemerides = &(GV.edat);

  /* characterize the output time-series */
  if ( ! uvar_exactSignal )	/* GeneratePulsarSignal() uses 'idealized heterodyning' */
    {
      params.samplingRate 	= 2.0 * GV.fBand_eff;	/* sampling rate of time-series (=2*frequency-Band) */
      params.fHeterodyne  	= GV.fmin_eff;		/* heterodyning frequency for output time-series */
    }
  else	/* in the exact-signal case: don't do heterodyning, sample at least twice highest frequency */
    {
      params.samplingRate 	= myMax( 2.0 * (params.pulsar.f0 + 2 ), 2*(GV.fmin_eff + GV.fBand_eff ) );
      params.fHeterodyne 	= 0;
    }

  /* set-up main-loop according to 'generation-mode' (all-at-once' or 'per-sft') */
  switch ( uvar_generationMode )
    {
    case GENERATE_ALL_AT_ONCE:
      params.duration     = GV.duration;
      numchunks = 1;
      break;
    case GENERATE_PER_SFT:
      params.duration = (UINT4) ceil(uvar_Tsft);
      numchunks = GV.timestamps->length;
      break;
    default:
      LALPrintError ("\nIllegal value for generationMode %d\n\n", uvar_generationMode);
      return MAKEFAKEDATAC_EARG;
      break;
    } /* switch generationMode */

  /* ----------
   * Main loop: produce time-series and turn it into SFTs,
   * either all-at-once or per-sft 
   * ----------*/
  for ( i_chunk = 0; i_chunk < numchunks; i_chunk++ )
    {
      params.startTimeGPS = GV.timestamps->data[i_chunk];
      
      /*----------------------------------------
       * generate the signal time-series 
       *----------------------------------------*/
      if ( uvar_exactSignal ) 
	{
	  LAL_CALL ( LALSimulateExactPulsarSignal (&status, &Tseries, &params), &status );
	} 
      else 
	{
	  LAL_CALL ( LALGeneratePulsarSignal(&status, &Tseries, &params), &status );
	}

      /* for HARDWARE-INJECTION: 
       * before the first chunk we send magic number and chunk-length to stdout 
       */
      if ( uvar_hardwareTDD && (i_chunk == 0) )
	{
	  REAL4 magic = 1234.5;
	  UINT4 length = Tseries->data->length;
	  if ( (1 != fwrite ( &magic, sizeof(magic), 1, stdout ))
	       || (1 != fwrite(&length, sizeof(INT4), 1, stdout)) )
	    {
	      perror ("Failed to write to stdout");
	      exit (MAKEFAKEDATAC_EFILE);
	    }
	} /* if hardware-injection and doing first chunk */


      /* add Gaussian noise if requested */
      if ( GV.noiseSigma > 0) {
	LAL_CALL ( AddGaussianNoise(&status, Tseries, Tseries, (REAL4)(GV.noiseSigma) ), &status);
      }

      /* output ASCII time-series if requested */
      if ( uvar_TDDfile )
	{
	  FILE *fp;
	  CHAR *fname = LALCalloc (1, strlen(uvar_TDDfile) + 10 );
	  sprintf (fname, "%s.%02d", uvar_TDDfile, i_chunk);
	  
	  if ( (fp = fopen (fname, "w")) == NULL)
	    {
	      perror ("Error opening outTDDfile for writing");
	      LALPrintError ("TDDfile = %s\n", fname );
	      exit ( MAKEFAKEDATAC_EFILE );
	    }
	  
	  write_timeSeriesR4(fp, Tseries);
	  fclose(fp);
	  LALFree (fname);
	} /* if outputting ASCII time-series */


      /* if hardware injection: send timeseries in binary-format to stdout */
      if ( uvar_hardwareTDD )
	{
	  UINT4  length = Tseries->data->length;
	  REAL4 *datap = Tseries->data->data;
	  
	  if ( length != fwrite (datap, sizeof(datap[0]), length, stdout) )
	    {
	      perror( "Fatal error in writing binary data to stdout\n");
	      exit (MAKEFAKEDATAC_EBINARYOUT);
	    }
	  fflush (stdout);
	
	} /* if hardware injections */

      /*----------------------------------------
       * last step: turn this timeseries into SFTs
       * and output them to disk 
       *----------------------------------------*/
      if (uvar_outSFTbname)
	{
	  SFTParams sftParams = empty_sftParams;
	  LIGOTimeGPSVector ts;
	  SFTVector noise;
	  
	  sftParams.Tsft = uvar_Tsft;
	  sftParams.make_v2SFTs = 1;

	  switch (uvar_generationMode)
	    {
	    case GENERATE_ALL_AT_ONCE:
	      sftParams.timestamps = GV.timestamps;
	      sftParams.noiseSFTs = GV.noiseSFTs;
	      break;
	    case GENERATE_PER_SFT:
	      ts.length = 1;
	      ts.data = &(GV.timestamps->data[i_chunk]);
	      sftParams.timestamps = &(ts);
	      sftParams.noiseSFTs = NULL;

	      if ( GV.noiseSFTs )
		{
		  noise.length = 1;
		  noise.data = &(GV.noiseSFTs->data[i_chunk]);
		  sftParams.noiseSFTs = &(noise);
		}

	      break;

	    default:
	      LALPrintError ("\ninvalid Value for generationMode\n");
	      return MAKEFAKEDATAC_EBAD;
	      break;
	    }
	  /* get SFTs from timeseries */
	  LAL_CALL ( LALSignalToSFTs(&status, &SFTs, Tseries, &sftParams), &status);
	  
	  /* extract requested band if necessary (eg in the exact-signal case) */
	  if ( uvar_exactSignal )
	    {
	      SFTVector *outSFTs = NULL;
	      LAL_CALL ( LALExtractSFTBand ( &status, &outSFTs, SFTs, GV.fmin_eff, GV.fBand_eff ), &status );
	      LAL_CALL ( LALDestroySFTVector ( &status, &SFTs ), &status );
	      SFTs = outSFTs;
	    }

	  if ( uvar_outSFTv1 ) 		/* write output-SFTs using the SFT-v1 format */
	    {
	      CHAR *fname = LALCalloc (1, strlen (uvar_outSFTbname) + 10);

	      for (i=0; i < SFTs->length; i++)
		{
		  sprintf (fname, "%s.%05d", uvar_outSFTbname, i_chunk*SFTs->length + i);
		  LAL_CALL ( LALWrite_v2SFT_to_v1file ( &status, &(SFTs->data[i]), fname ), &status );
		}  
	      LALFree (fname);
	    } /* if outSFTv1 */
	  else
	    {	/* write standard v2-SFTs */
	      CHAR *comment = NULL;
	      CHAR *logstr = NULL;

	      /* check that user didn't follow v1-habits and tried to give us a base filename */
	      if ( ! is_directory ( uvar_outSFTbname ) ) 
		{
		  fprintf (stderr, "\nERROR: the --outSFTbname '%s' is not a directory!\n", uvar_outSFTbname);
		  fprintf (stderr, "------------------------------------------------------------\n");
		  fprintf (stderr, "NOTE: v2-SFTs are written using the SFTv2 filename-convention!\n");
		  fprintf (stderr, "==> therefore, only an (existing) directory-name may be given to --outSFTbname, but no basename!\n");
		  fprintf (stderr, "------------------------------------------------------------\n\n");
		  return MAKEFAKEDATAC_EBAD;
		}
	      LAL_CALL ( LALUserVarGetLog ( &status, &logstr,  UVAR_LOGFMT_CMDLINE ), &status );
	      comment = LALCalloc ( 1, 512 + strlen ( logstr ) );
	      sprintf ( comment, "Generated by $Id$:\n%s", logstr );
	      LAL_CALL ( LALWriteSFTVector2Dir (&status, SFTs, uvar_outSFTbname, comment, "mfdv4" ), &status );
	      LALFree ( logstr );
	      LALFree ( comment );
	    }
	} /* if outSFTbname */

      /* free memory */
      if (Tseries) 
	{
	  LAL_CALL (LALDestroyVector(&status, &(Tseries->data)), &status);
	  LALFree (Tseries);
	  Tseries = NULL;
	}

      if (SFTs) 
	{
	  LAL_CALL (LALDestroySFTVector(&status, &SFTs), &status);
	  SFTs = NULL;
	}

    } /* for i_chunk < numchunks */
  
  LAL_CALL (FreeMem(&status, &GV), &status);	/* free the rest */
  
  LALCheckMemoryLeaks(); 

  return 0;
} /* main */

/**
 *  Handle user-input and set up shop accordingly, and do all 
 * consistency-checks on user-input.
 */
void
InitMakefakedata (LALStatus *status, ConfigVars_t *cfg, int argc, char *argv[])
{
 
  INITSTATUS( status, "InitMakefakedata", rcsid );
  ATTATCHSTATUSPTR (status);

  /* register all user-variables */
  TRY (InitUserVars(status->statusPtr), status);	  

  /* read cmdline & cfgfile  */	
  TRY (LALUserVarReadAllInput(status->statusPtr, argc,argv), status);  

  if (uvar_help) 	/* if help was requested, we're done */
    exit (0);

  /* if requested, log all user-input and code-versions */
  if ( uvar_logfile ) {
    TRY ( WriteMFDlog(status->statusPtr, argv, uvar_logfile), status);
  }

  /* ---------- prepare vector of spindown parameters ---------- */
  {
    UINT4 msp = 0;	/* number of spindown-parameters */
    if (uvar_f3dot != 0) 		msp = 3;	/* counter number of spindowns */
    else if (uvar_f2dot != 0)	msp = 2;
    else if (uvar_f1dot != 0)	msp = 1;
    else 				msp = 0;
    if (msp) {
      /* memory not initialized, but ALL alloc'ed entries will be set below! */
      TRY (LALDCreateVector(status->statusPtr, &(cfg->spindown), msp), status);
    }
    switch (msp) 
      {
      case 3:
	cfg->spindown->data[2] = uvar_f3dot;
      case 2:
	cfg->spindown->data[1] = uvar_f2dot;
      case 1:
	cfg->spindown->data[0] = uvar_f1dot;
	break;
      case 0:
	break;
      default:
	LALPrintError ("\nmsp = %d makes no sense to me...\n\n", msp);
	ABORT (status,  MAKEFAKEDATAC_EBAD,  MAKEFAKEDATAC_MSGEBAD);
      } /* switch(msp) */

  } /* END: prepare spindown parameters */

  /* ---------- prepare detector ---------- */
  {
    LALDetector *site;
    if ( ( site = XLALGetSiteInfo ( uvar_detector ) ) == NULL ) {
      ABORT (status, MAKEFAKEDATAC_EBAD, MAKEFAKEDATAC_MSGEBAD);
    }
    cfg->site = (*site);
    LALFree ( site );
  }
  /* ---------- determine requested signal- start + duration ---------- */
  {
    /* check input consistency: *uvar_timestampsFile, uvar_startTime, uvar_duration */
    BOOLEAN haveStart, haveDuration, haveTimestamps, haveOverlap;
    haveStart = LALUserVarWasSet(&uvar_startTime);
    haveDuration = LALUserVarWasSet(&uvar_duration);
    haveTimestamps = LALUserVarWasSet(&uvar_timestampsFile);
    haveOverlap = LALUserVarWasSet ( &uvar_SFToverlap );

    /*-------------------- special case: Hardware injection ---------- */
    /* don't allow timestamps-file and SFT-output */
    if ( uvar_hardwareTDD )
      {
	if (haveTimestamps || !( haveStart && haveDuration ) ) 
	  {
	    LALPrintError ("\nHardware injection: please specify 'startTime' and 'duration'\n\n");
	    ABORT (status,  MAKEFAKEDATAC_EBAD,  MAKEFAKEDATAC_MSGEBAD);
	  }
	if ( LALUserVarWasSet( &uvar_outSFTbname ) )
	  {
	    LALPrintError ("\nHardware injection mode is incompatible with producing SFTs\n\n");
	    ABORT (status,  MAKEFAKEDATAC_EBAD,  MAKEFAKEDATAC_MSGEBAD);
	  }
      } /* if hardware-injection */
    
    /* ----- determine timestamps covering the observation period */
    if ( haveTimestamps )
      {
	LIGOTimeGPSVector *timestamps = NULL;
	if ( haveStart || haveDuration || haveOverlap )
	  {
	    LALPrintError ( "\nUsing --timestampsFile is incompatible with either of --startTime, --duration or --SFToverlap\n\n");
	    ABORT (status,  MAKEFAKEDATAC_EBAD,  MAKEFAKEDATAC_MSGEBAD);
	  }
	TRY (LALReadTimestampsFile(status->statusPtr, &timestamps, uvar_timestampsFile), status);
	cfg->timestamps = timestamps;
      } /* haveTimestamps */
    else
      {
	REAL8 tStep = uvar_Tsft - uvar_SFToverlap;
	LIGOTimeGPS tStart;
	if ( !haveStart || !haveDuration )
	  {
	    LALPrintError ( "\nYou need to specify either --timestampsFile OR --startTime and --duration \n\n");
	    ABORT (status,  MAKEFAKEDATAC_EBAD,  MAKEFAKEDATAC_MSGEBAD);
	  }
	if ( uvar_SFToverlap > uvar_Tsft )
	  {
	    LALPrintError ("\nERROR: --SFToverlap cannot be larger than --Tsft!\n\n");
	    ABORT (status,  MAKEFAKEDATAC_EBAD,  MAKEFAKEDATAC_MSGEBAD);	    
	  }
	/* for simplicity we *ALWAYS* use timestamps, 
	 * so we generate them now as the user didnt' specify them */
	XLALFloatToGPS ( &tStart, uvar_startTime );
	TRY(LALMakeTimestamps(status->statusPtr, &(cfg->timestamps), tStart, uvar_duration, tStep ), status);
      } /* !haveTimestamps */

    /* ----- figure out start-time and duration ----- */
    {
      LIGOTimeGPS t1, t0;
      REAL8 duration;
      
      t0 = cfg->timestamps->data[0];
      t1 = cfg->timestamps->data[cfg->timestamps->length - 1 ];
      TRY (LALDeltaFloatGPS(status->statusPtr, &duration, &t1, &t0), status);
      duration += uvar_Tsft;
      
      cfg->startTimeGPS = cfg->timestamps->data[0];
      cfg->duration = (UINT4)ceil ( duration );	/* round up to seconds */
    }

    if ( cfg->duration < uvar_Tsft )
      {
	LALPrintError ("\nERROR: requested duration of %d sec is less than minimal "
		       "chunk-size of Tsft =%.0f sec.\n\n",
		       uvar_duration, uvar_Tsft);
	ABORT (status,  MAKEFAKEDATAC_EBAD,  MAKEFAKEDATAC_MSGEBAD);
      }

 
  } /* END: setup signal start + duration */

  /*----------------------------------------------------------------------*/
  /* currently there are only two modes: [uvar_generationMode]
   * 1) produce the whole timeseries at once [default], 
   *       [GENERATE_ALL_AT_ONCE]
   * 2) produce timeseries for each SFT,
   *       [GENERATE_PER_SFT]
   * 
   * Mode 2 which is useful if 1) is too memory-intensive (e.g. for hardware-injections)
   *
   * intermediate modes would require a bit more work because the 
   * case with specified timestamps (allowing for gaps) would be 
   * a bit more tricky ==> this is left for future extensions if found useful
   */
  if ( (uvar_generationMode < 0) || (uvar_generationMode >= GENERATE_LAST) )
    {
      LALPrintError ("\nIllegal input for 'generationMode': must lie within [0, %d]\n\n", 
		     GENERATE_LAST -1);
      ABORT (status,  MAKEFAKEDATAC_EBAD,  MAKEFAKEDATAC_MSGEBAD);
    }

  /* this is a violation of the UserInput-paradigm that no user-variables 
   * should by modified by the code, but this is the easiest way to do
   * this here, and the log-output of user-variables will not be changed
   * by this [it's already been written], so in this case it should be safe..
   */
  if ( uvar_hardwareTDD )
    uvar_generationMode = GENERATE_PER_SFT;

  /* ---------- for SFT output: calculate effective fmin and Band ---------- */
  if ( LALUserVarWasSet( &uvar_outSFTbname ) )
    {
      UINT4 imin, imax;
      volatile REAL8 dFreq = 1.0 / uvar_Tsft;
      volatile REAL8 tmp;
      REAL8 fMax, fMax_eff, fMin_eff;

      /* calculate "effective" fmin from uvar_fmin: following makefakedata_v2, we
       * make sure that fmin_eff * Tsft = integer, such that freqBinIndex corresponds
       * to a frequency-index of the non-heterodyned signal.
       */
      tmp = uvar_fmin / dFreq;	/* NOTE: don't "simplify" this: we try to make sure
				 * the result of this will be guaranteed to be IEEE-compliant,
				 * and identical to other locations, such as in SFT-IO
				 */
      imin = (UINT4) floor( tmp );
      fMin_eff = (REAL8)imin * dFreq;

      fMax = uvar_fmin + uvar_Band;
      tmp = fMax / dFreq;
      imax = (UINT4) ceil (tmp);
      fMax_eff = (REAL8)imax * dFreq;

      /* Increase Band correspondingly. */
      cfg->fmin_eff = fMin_eff;
      cfg->fBand_eff = 1.0 * (imax - imin) * dFreq;

      if ( lalDebugLevel )
	{
	  if ( abs(cfg->fmin_eff - uvar_fmin)> LAL_REAL8_EPS 
	       || abs(cfg->fBand_eff - uvar_Band) > LAL_REAL8_EPS )
	    printf("\nWARNING: for SFT-creation we had to adjust (fmin,Band) to"
		   " fmin_eff=%.20g and Band_eff=%.20g\n\n", cfg->fmin_eff, cfg->fBand_eff);
	}
      
    } /* END: SFT-specific corrections to fmin and Band */
  else
    { /* producing pure time-series output ==> no adjustments necessary */
      cfg->fmin_eff = uvar_fmin;
      cfg->fBand_eff = uvar_Band;
    }


  /* -------------------- Prepare quantities for barycentering -------------------- */
  {
    UINT4 len;
    INT4 leapSecs;
    LALLeapSecFormatAndAcc leapParams;
    EphemerisData edat = empty_edat;
    CHAR *earthdata, *sundata;

    len = strlen(uvar_ephemYear) + 20;

    if (LALUserVarWasSet(&uvar_ephemDir) ) 
      len += strlen (uvar_ephemDir);
      
    if ( (earthdata = LALCalloc(1, len)) == NULL) {
      ABORT (status, MAKEFAKEDATAC_EMEM, MAKEFAKEDATAC_MSGEMEM );
    }
    if ( (sundata = LALCalloc(1, len)) == NULL) {
      ABORT (status, MAKEFAKEDATAC_EMEM, MAKEFAKEDATAC_MSGEMEM );
    }

    if (LALUserVarWasSet(&uvar_ephemDir) ) 
      {
	sprintf(earthdata, "%s/earth%s.dat", uvar_ephemDir, uvar_ephemYear);
	sprintf(sundata, "%s/sun%s.dat", uvar_ephemDir, uvar_ephemYear);
      }
    else
      {
	sprintf(earthdata, "earth%s.dat", uvar_ephemYear);
	sprintf(sundata, "sun%s.dat",  uvar_ephemYear);
      }

    edat.ephiles.earthEphemeris = earthdata;
    edat.ephiles.sunEphemeris   = sundata;
        
    /* get leap-seconds since start of GPS-time */
    leapParams.format =  LALLEAPSEC_GPSUTC;
    leapParams.accuracy = LALLEAPSEC_STRICT;	/* complain about missing leap-info */
    TRY ( LALLeapSecs(status->statusPtr, &leapSecs,  &(cfg->startTimeGPS), &leapParams), status);
    edat.leap = (INT2) leapSecs;

    /* Init ephemerides */  
    TRY( LALInitBarycenter(status->statusPtr, &edat), status);   
    LALFree(earthdata);
    LALFree(sundata);

    cfg->edat = edat;	/* struct-copy */

  } /* END: prepare barycentering routines */

  /* -------------------- handle binary orbital params if given -------------------- */

  /* Consistency check: if any orbital parameters specified, we need all of them (except for nano-seconds)! */ 
  {
    BOOLEAN set1 = LALUserVarWasSet(&uvar_orbitSemiMajorAxis);
    BOOLEAN set2 = LALUserVarWasSet(&uvar_orbitEccentricity);
    BOOLEAN set3 = LALUserVarWasSet(&uvar_orbitPeriod);
    BOOLEAN set4 = LALUserVarWasSet(&uvar_orbitArgPeriapse);
    BOOLEAN set5 = LALUserVarWasSet(&uvar_orbitTperiSSBsec);
    BOOLEAN set6 = LALUserVarWasSet(&uvar_orbitTperiSSBns);
    if (set1 || set2 || set3 || set4 || set5 || set6)
    {
      if ( (uvar_orbitSemiMajorAxis > 0) && !(set1 && set2 && set3 && set4 && set5) ) 
	{
	  LALPrintError("\nPlease either specify  ALL orbital parameters or NONE!\n\n");
	  ABORT (status, MAKEFAKEDATAC_EBAD, MAKEFAKEDATAC_MSGEBAD);
	}
      cfg->binaryPulsar = TRUE;

      if ( (uvar_orbitEccentricity < 0) || (uvar_orbitEccentricity > 1) )
	{
	  LALPrintError ("\nEccentricity has to lie within [0, 1]\n\n");
	  ABORT (status, MAKEFAKEDATAC_EBAD, MAKEFAKEDATAC_MSGEBAD);
	}

    } /* if one or more orbital parameters were set */
  } /* END: binary orbital params */


  /* -------------------- handle NOISE params -------------------- */
  {
    /* if real noise-SFTs: load them in now */
    if ( uvar_noiseSFTs )
      {
	REAL8 fMin, fMax;
	SFTCatalog *catalog = NULL;
	SFTConstraints constraints = empty_SFTConstraints;

	if ( (constraints.detector = XLALGetChannelPrefix ( uvar_detector )) == NULL ) {
	  ABORT ( status,  MAKEFAKEDATAC_EBAD,  MAKEFAKEDATAC_MSGEBAD );
	}

	TRY ( LALSFTdataFind( status->statusPtr, &catalog, uvar_noiseSFTs, &constraints ), status );
	LALFree ( constraints.detector );

	fMin = cfg->fmin_eff;
	fMax = fMin + cfg->fBand_eff;
	TRY ( LALLoadSFTs( status->statusPtr, &(cfg->noiseSFTs), catalog, fMin, fMax ), status );
	TRY ( LALDestroySFTCatalog ( status->statusPtr, &catalog ), status );

      } /* if uvar_noiseSFTs */

    if ( LALUserVarWasSet ( &uvar_noiseSigma ) && LALUserVarWasSet ( &uvar_noiseSqrtSh ) )
      {
	LALPrintError ( "\nUse only one of '--noiseSigma' and '--noiseSqrtSh' to specify Gaussian noise!\n\n");
	ABORT ( status,  MAKEFAKEDATAC_EBAD,  MAKEFAKEDATAC_MSGEBAD );
      }
    if ( LALUserVarWasSet ( &uvar_noiseSigma ) )
      cfg->noiseSigma = uvar_noiseSigma;
    else if ( LALUserVarWasSet ( &uvar_noiseSqrtSh ) )	/* convert Sh -> sigma */
      cfg->noiseSigma = uvar_noiseSqrtSh * sqrt ( cfg->fBand_eff );
    else
      cfg->noiseSigma = 0;
    
  } /* END: Noise params */


  /* ----- set "pulsar reference time", i.e. SSB-time at which pulsar params are defined ---------- */
  TRY ( LALFloatToGPS(status->statusPtr, &(cfg->refTime), &uvar_refTime), status);

  /* ---------- has the user specified an actuation-function file ? ---------- */
  if ( uvar_actuation ) 
    {
      /* currently we only allow using a transfer-function for hardware-injections */
      if (!uvar_hardwareTDD )
	{
	  LALPrintError ("\nError: use of an actuation/transfer function "
			 "restricted to hardare-injections\n\n");
	  ABORT (status,  MAKEFAKEDATAC_EBAD,  MAKEFAKEDATAC_MSGEBAD);
	}
      else {
	TRY ( LoadTransferFunctionFromActuation( status->statusPtr, &(cfg->transfer), 
						 uvar_actuationScale, uvar_actuation), status);
      }

    } /* if uvar_actuation */
  
  if ( !uvar_actuation && LALUserVarWasSet(&uvar_actuationScale) )
    {
      LALPrintError ("\nActuation-scale was specified without actuation-function file!\n\n");
      ABORT (status,  MAKEFAKEDATAC_EBAD,  MAKEFAKEDATAC_MSGEBAD);
    }


  DETATCHSTATUSPTR (status);
  RETURN (status);

} /* InitMakefakedata() */



/** 
 * register all our "user-variables" 
 */
void
InitUserVars (LALStatus *status)
{
  INITSTATUS( status, "InitUserVars", rcsid );
  ATTATCHSTATUSPTR (status);

  /* ---------- set a few defaults ----------  */
  uvar_ephemYear = LALCalloc(1, strlen(EPHEM_YEARS)+1);
  strcpy (uvar_ephemYear, EPHEM_YEARS);

#define DEFAULT_EPHEMDIR "env LAL_DATA_PATH"
  uvar_ephemDir = LALCalloc(1, strlen(DEFAULT_EPHEMDIR)+1);
  strcpy (uvar_ephemDir, DEFAULT_EPHEMDIR);

  uvar_help = FALSE;
  uvar_Tsft = 1800;
  uvar_f1dot = 0.0;
  uvar_f2dot = 0.0;
  uvar_f3dot = 0.0;

  uvar_noiseSigma = 0;
  uvar_noiseSqrtSh = 0;
  uvar_noiseSFTs = NULL;

  uvar_hardwareTDD = FALSE;

  uvar_fmin = 0;	/* no heterodyning by default */
  uvar_Band = 8192;	/* 1/2 LIGO sampling rate by default */

  uvar_TDDfile = NULL;

  uvar_logfile = NULL;

  /* per default we generate the whole timeseries first (except for hardware-injections)*/
  uvar_generationMode = GENERATE_ALL_AT_ONCE;

  uvar_refTime = 0.0;

  uvar_actuation = NULL;
  uvar_actuationScale = - 1.0;	/* seems to be the LIGO-default */

  uvar_exactSignal = FALSE;

  uvar_outSFTv1 = FALSE;


  /* ---------- register all our user-variable ---------- */
  LALregBOOLUserVar(status,   help,	'h', UVAR_HELP    , "Print this help/usage message");

  /* output options */
  LALregSTRINGUserVar(status, outSFTbname,'n', UVAR_OPTIONAL, "Output directory for output SFTs (include file-basename *ONLY* for v1-SFTs!) ");

  LALregSTRINGUserVar(status, TDDfile,	't', UVAR_OPTIONAL, "Filename for output of time-series");
  LALregBOOLUserVar(status,   hardwareTDD,'b', UVAR_OPTIONAL, "Hardware injection: output TDD in binary format (implies generationMode=1)");

  LALregSTRINGUserVar(status, logfile,	'l', UVAR_OPTIONAL, "Filename for log-output");

  /* detector and ephemeris */
  LALregSTRINGUserVar(status, detector,  'I', UVAR_REQUIRED, "Detector: 'G1','L1','H1,'H2',...");

  LALregSTRINGUserVar(status, actuation,   0,  UVAR_OPTIONAL, "Filname containing actuation function of this detector");
  LALregREALUserVar(status,   actuationScale, 0, UVAR_OPTIONAL, 
		    "(Signed) scale-factor to apply to the actuation-function.");

  LALregSTRINGUserVar(status, ephemDir,	'E', UVAR_OPTIONAL, "Directory path for ephemeris files");
  LALregSTRINGUserVar(status, ephemYear, 	'y', UVAR_OPTIONAL, "Year (or range of years) of ephemeris files to be used");

  /* start + duration of timeseries */
  LALregINTUserVar(status,   startTime,	'G', UVAR_OPTIONAL, "Start-time of requested signal in detector-frame (GPS seconds)");
  LALregINTUserVar(status,   duration,	 0,  UVAR_OPTIONAL, "Duration of requested signal in seconds");
  LALregSTRINGUserVar(status,timestampsFile,0, UVAR_OPTIONAL, "Timestamps file");
  
  /* generation-mode of timeseries: all-at-once or per-sft */
  LALregINTUserVar(status,   generationMode, 0,  UVAR_OPTIONAL, "How to generate timeseries: 0=all-at-once, 1=per-sft");

  /* sampling and heterodyning frequencies */
  LALregREALUserVar(status,   fmin,	 0 , UVAR_OPTIONAL, "Lowest frequency in output SFT (= heterodyning frequency)");
  LALregREALUserVar(status,   Band,	 0 , UVAR_OPTIONAL, "bandwidth of output SFT in Hz (= 1/2 sampling frequency)");

  /* SFT properties */
  LALregREALUserVar(status,   Tsft, 	 0 , UVAR_OPTIONAL, "Time baseline of one SFT in seconds");
  LALregREALUserVar(status,   SFToverlap,0 , UVAR_OPTIONAL, "Overlap between successive SFTs in seconds");

  /* pulsar params */
  LALregREALUserVar(status,   refTime, 	'S', UVAR_OPTIONAL, "Pulsar reference time tRef in SSB (if 0: use startTime -> SSB)");
  LALregREALUserVar(status,   longitude,	 0 , UVAR_REQUIRED, "Right ascension [radians] alpha of pulsar");
  LALregREALUserVar(status,   latitude, 	 0 , UVAR_REQUIRED, "Declination [radians] delta of pulsar");
  LALregREALUserVar(status,   aPlus,	 0 , UVAR_REQUIRED, "Plus polarization amplitude aPlus");
  LALregREALUserVar(status,   aCross, 	 0 , UVAR_REQUIRED, "Cross polarization amplitude aCross");
  LALregREALUserVar(status,   psi,  	 0 , UVAR_REQUIRED, "Polarization angle psi");
  LALregREALUserVar(status,   phi0,	 0 , UVAR_REQUIRED, "Initial phase phi");
  LALregREALUserVar(status,   f0,  	 0 , UVAR_REQUIRED, "Gravitational wave-frequency f0 at tRef");
  LALregREALUserVar(status,   f1dot,  	 0 , UVAR_OPTIONAL, "First spindown parameter f'");
  LALregREALUserVar(status,   f2dot,  	 0 , UVAR_OPTIONAL, "Second spindown parameter f''");
  LALregREALUserVar(status,   f3dot,  	 0 , UVAR_OPTIONAL, "Third spindown parameter f'''");

  /* binary-system orbital parameters */
  LALregREALUserVar(status,   orbitSemiMajorAxis, 0, UVAR_OPTIONAL, "Projected orbital semi-major axis in seconds (a/c)");
  LALregREALUserVar(status,   orbitEccentricity,  0, UVAR_OPTIONAL, "Orbital eccentricity");
  LALregINTUserVar(status,    orbitTperiSSBsec,   0, UVAR_OPTIONAL, "'observed' (SSB) time of periapsis passage. Seconds.");
  LALregINTUserVar(status,    orbitTperiSSBns,    0, UVAR_OPTIONAL, "'observed' (SSB) time of periapsis passage. Nanoseconds.");
  LALregREALUserVar(status,   orbitPeriod,        0, UVAR_OPTIONAL, "Orbital period (seconds)");
  LALregREALUserVar(status,   orbitArgPeriapse,   0, UVAR_OPTIONAL, "Argument of periapsis (radians)");                            

  /* noise */
  LALregSTRINGUserVar(status, noiseSFTs,	'D', UVAR_OPTIONAL, "Glob-like pattern specifying noise-SFTs to be added to signal");  
  LALregREALUserVar(status,   noiseSigma,	 0 , UVAR_OPTIONAL, "Gaussian noise with standard-deviation sigma");
  LALregREALUserVar(status,   noiseSqrtSh,	 0 , UVAR_OPTIONAL, "ALTERNATIVE: Gaussian noise with single-sided PSD sqrt(Sh)");
  
  /* signal precision-level */
  LALregBOOLUserVar(status,  exactSignal,	 0, UVAR_DEVELOPER, "Generate signal time-series as exactly as possible (slow).");

  LALregBOOLUserVar(status,  outSFTv1,	 	 0, UVAR_OPTIONAL, "Write output-SFTs in obsolete SFT-v1 format." );
  
  DETATCHSTATUSPTR (status);
  RETURN (status);

} /* InitUserVars() */


/** 
 * This routine frees up all the memory.
 */
void FreeMem (LALStatus* status, ConfigVars_t *cfg)
{

  INITSTATUS( status, "FreeMem", rcsid );
  ATTATCHSTATUSPTR (status);

  
  /* Free config-Variables and userInput stuff */
  TRY (LALDestroyUserVars(status->statusPtr), status);

  /* free timestamps if any */
  if (cfg->timestamps){
    TRY (LALDestroyTimestampVector(status->statusPtr, &(cfg->timestamps)), status);
  }

  /* free spindown-vector (REAL8) */
  if (cfg->spindown) {
    TRY (LALDDestroyVector(status->statusPtr, &(cfg->spindown)), status);
  }

  /* free noise-SFTs */
  if (cfg->noiseSFTs) {
    TRY (LALDestroySFTVector(status->statusPtr, &(cfg->noiseSFTs)), status);
  }

  /* free transfer-function if we have one.. */
  if ( cfg->transfer ) {
    TRY ( LALCDestroyVector(status->statusPtr, &(cfg->transfer->data)), status);
    LALFree (cfg->transfer);
  }

  /* Clean up earth/sun Ephemeris tables */
  LALFree(cfg->edat.ephemE);
  LALFree(cfg->edat.ephemS);

  DETATCHSTATUSPTR (status);
  RETURN (status);

} /* FreeMem() */

/**
 * Generate Gaussian noise with standard-deviation sigma, add it to inSeries.
 * returns outSeries
 *
 * NOTE: inSeries is allowed to be identical to outSeries!
 *
 */
void
AddGaussianNoise (LALStatus* status, REAL4TimeSeries *outSeries, REAL4TimeSeries *inSeries, REAL4 sigma)
{

  REAL4Vector    *v1 = NULL;
  RandomParams   *randpar = NULL;
  UINT4          numPoints, i;
  INT4 seed;
  FILE *devrandom;
  REAL4Vector *bak;

  INITSTATUS( status, "AddGaussianNoise", rcsid );
  ATTATCHSTATUSPTR (status);
  

  ASSERT ( outSeries, status, MAKEFAKEDATAC_EBAD, MAKEFAKEDATAC_MSGEBAD);
  ASSERT ( inSeries, status, MAKEFAKEDATAC_EBAD, MAKEFAKEDATAC_MSGEBAD);
  ASSERT ( outSeries->data->length == inSeries->data->length, status, MAKEFAKEDATAC_EBAD, MAKEFAKEDATAC_MSGEBAD);
  
  numPoints = inSeries->data->length;

  TRY (LALCreateVector(status->statusPtr, &v1, numPoints), status);
  
  /*
   * Modified so as to not create random number parameters with seed
   * drawn from clock.  Seconds don't change fast enough and sft's
   * look alike.  We open /dev/urandom and read a 4 byte integer from
   * it and use that as our seed.  Note: /dev/random is slow after the
   * first, few accesses.
   */
  
  if ( (devrandom = fopen("/dev/urandom","r")) == NULL )
    {
      LALPrintError ("\nCould not open '/dev/urandom'\n\n");
      ABORT (status, MAKEFAKEDATAC_EFILE, MAKEFAKEDATAC_MSGEFILE);
    }

  if ( fread( (void*)&seed, sizeof(INT4), 1, devrandom) != 1)
    {
      LALPrintError("\nCould not read from '/dev/urandom'\n\n");
      fclose(devrandom);
      ABORT (status, MAKEFAKEDATAC_EFILE, MAKEFAKEDATAC_MSGEFILE);
    }

  fclose(devrandom);
  
  TRY (LALCreateRandomParams(status->statusPtr, &randpar, seed), status);

  TRY (LALNormalDeviates(status->statusPtr, v1, randpar), status);

  for (i = 0; i < numPoints; i++)
    outSeries->data->data[i] = inSeries->data->data[i] + sigma * v1->data[i];

  /* copy the rest of the time-series structure */
  bak = outSeries->data;
  outSeries = inSeries;		/* copy all struct-entries */
  outSeries->data = bak;	/* restore data-pointer */


  /* destroy randpar*/
  TRY (LALDestroyRandomParams(status->statusPtr, &randpar), status);
  
  /*   destroy v1 */
  TRY (LALDestroyVector(status->statusPtr, &v1), status);


  DETATCHSTATUSPTR (status);
  RETURN (status);

} /* AddGaussianNoise() */


/**
 * 
 * For signals from NS in binary orbits: "Translate" our input parameters 
 * into those required by LALGenerateSpinOrbitCW() (see LAL doc for equations)
 * 
 */
void
GetOrbitalParams (LALStatus *status, BinaryOrbitParams *orbit)
{
  REAL8 OneMEcc;
  REAL8 OnePEcc;
  REAL8 correction;
  LIGOTimeGPS TperiTrue;
  LIGOTimeGPS TperiSSB;
  
  INITSTATUS( status, "GetOrbitalParams", rcsid );
  ATTATCHSTATUSPTR (status);
  
  OneMEcc = 1.0 - uvar_orbitEccentricity;
  OnePEcc = 1.0 + uvar_orbitEccentricity;

  /* we need to convert the observed time of periapse passage to the "true" time of periapse passage */
  /* this correction is due to the light travel time from binary barycenter to the source at periapse */
  /* it is what Teviets codes require */
  TperiSSB.gpsSeconds = uvar_orbitTperiSSBsec;
  TperiSSB.gpsNanoSeconds = uvar_orbitTperiSSBns;
  correction = uvar_orbitSemiMajorAxis * OneMEcc * sin(uvar_orbitArgPeriapse);

  TRY (LALAddFloatToGPS(status->statusPtr, &TperiTrue, &TperiSSB, -correction), status);

  orbit->orbitEpoch = TperiTrue;
  orbit->omega = uvar_orbitArgPeriapse;
  orbit->rPeriNorm = uvar_orbitSemiMajorAxis * OneMEcc;
  orbit->oneMinusEcc = OneMEcc;
  orbit->angularSpeed = (LAL_TWOPI/uvar_orbitPeriod) * sqrt(OnePEcc/(OneMEcc*OneMEcc*OneMEcc));

  DETATCHSTATUSPTR (status);
  RETURN(status);

} /* GetOrbitalParams() */


/***********************************************************************/
/** Log the all relevant parameters of this run into a log-file.
 * The name of the log-file used is uvar_logfile 
 * <em>NOTE:</em> Currently this function only logs the user-input and code-versions.
 */
void
WriteMFDlog (LALStatus *status, char *argv[], const char *logfile)
{
    CHAR *logstr = NULL;
    CHAR command[512] = "";
    FILE *fplog;

    INITSTATUS (status, "WriteMFDlog", rcsid);
    ATTATCHSTATUSPTR (status);

    if ( logfile == NULL )
      {
	LALPrintError ("\nERROR: WriteMFDlog() called with NULL logfile-name\n\n");
	ABORT( status, MAKEFAKEDATAC_EBAD, MAKEFAKEDATAC_MSGEBAD);
      }

    if ( (fplog = fopen(uvar_logfile, "wb" )) == NULL) {
      LALPrintError ("\nFailed to open log-file '%f' for writing.\n\n", uvar_logfile);
      ABORT (status, MAKEFAKEDATAC_EFILE, MAKEFAKEDATAC_MSGEFILE);
    }

    /* write out a log describing the complete user-input (in cfg-file format) */
    TRY (LALUserVarGetLog(status->statusPtr, &logstr,  UVAR_LOGFMT_CFGFILE), status);

    fprintf (fplog, "## LOG-FILE of Makefakedata run\n\n");
    fprintf (fplog, "# User-input:\n");
    fprintf (fplog, "# ----------------------------------------------------------------------\n\n");

    fprintf (fplog, logstr);
    LALFree (logstr);

    /* append an ident-string defining the exact CVS-version of the code used */
    fprintf (fplog, "\n\n# CVS-versions of executable:\n");
    fprintf (fplog, "# ----------------------------------------------------------------------\n");
    fclose (fplog);
    
    sprintf (command, "ident %s 2> /dev/null | sort -u >> %s", argv[0], uvar_logfile);
    system (command);   /* we currently don't check this. If it fails, we assume that */
                        /* one of the system-commands was not available, and */
                        /* therefore the CVS-versions will simply not be logged */


    DETATCHSTATUSPTR (status);
    RETURN(status);

} /* WriteMFDLog() */

/** 
 * Reads an actuation-function in format (r,\phi) from file 'fname', 
 * and returns the associated transfer-function as a COMPLEX8FrequencySeries (Re,Im)
 * The transfer-function T is simply the inverse of the actuation A, so T=A^-1.
 */
void
LoadTransferFunctionFromActuation(LALStatus *status, 
				  COMPLEX8FrequencySeries **transfer, /**< [out] transfer-function */
				  REAL8 actuationScale, 	/**< overall scale-factor to actuation*/
				  const CHAR *fname)		/**< file containing actuation-function*/
{
  LALParsedDataFile *fileContents = NULL;
  CHAR *thisline;
  UINT4 i, startline;
  COMPLEX8Vector *data = NULL;
  COMPLEX8FrequencySeries *ret = NULL;
  const CHAR *readfmt = "%" LAL_REAL8_FORMAT "%" LAL_REAL8_FORMAT "%" LAL_REAL8_FORMAT;
  REAL8 amp, phi;
  REAL8 f0, f1;

  INITSTATUS (status, "ReadActuationFunction", rcsid);
  ATTATCHSTATUSPTR (status);

  ASSERT (transfer, status, MAKEFAKEDATAC_EBAD, MAKEFAKEDATAC_MSGEBAD);
  ASSERT (*transfer == NULL, status, MAKEFAKEDATAC_EBAD, MAKEFAKEDATAC_MSGEBAD);
  ASSERT (fname != NULL, status, MAKEFAKEDATAC_EBAD, MAKEFAKEDATAC_MSGEBAD);

  TRY ( LALParseDataFile (status->statusPtr, &fileContents, fname), status);
  /* skip first line if containing NaN's ... */
  startline = 0;
  if ( strstr(fileContents->lines->tokens[startline], "NaN" ) != NULL )
    startline ++;

  LALCCreateVector (status->statusPtr, &data, fileContents->lines->nTokens - startline);
  BEGINFAIL(status)
    TRY ( LALDestroyParsedDataFile(status->statusPtr, &fileContents), status);    
  ENDFAIL(status);

  if ( (ret = LALCalloc(1, sizeof(COMPLEX8FrequencySeries))) == NULL)
    {
      LALDestroyParsedDataFile(status->statusPtr, &fileContents);
      LALFree (data);
      ABORT (status, MAKEFAKEDATAC_EMEM, MAKEFAKEDATAC_MSGEMEM);
    }

  LALSnprintf ( ret->name, LALNameLength-1, "Transfer-function from: %s", fname );
  ret->name[LALNameLength-1]=0;

  /* initialize loop */
  f0 = f1 = 0;

  for (i=startline; i < fileContents->lines->nTokens; i++)
    {
      thisline = fileContents->lines->tokens[i];

      f0 = f1;
      if ( 3 != sscanf (thisline, readfmt, &f1, &amp, &phi) )
	{
	  LALPrintError ("\nFailed to read 3 floats from line %d of file %s\n\n", i, fname);
	  goto failed;
	}
      
      if ( !gsl_finite(amp) || !gsl_finite(phi) )
	{
	  LALPrintError ("\nERROR: non-finite number encountered in actuation-function at f!=0. Line=%d\n\n", i);
	  goto failed;
	}

      /* first line: set f0 */
      if ( i == startline )
	ret->f0 = f1;

      /* second line: set deltaF */
      if ( i == startline + 1 )
	ret->deltaF = f1 - ret->f0;
	
      /* check constancy of frequency-step */
      if ( (f1 - f0 != ret->deltaF) && (i > startline) )
	{
	  LALPrintError ("\nIllegal frequency-step %f != %f in line %d of file %s\n\n",
			 (f1-f0), ret->deltaF, i, fname);
	  goto failed;
	}

      /* now convert into transfer-function and (Re,Im): T = A^-1 */
      data->data[i-startline].re = (REAL4)( cos(phi) / ( amp * actuationScale) );
      data->data[i-startline].im = (REAL4)(-sin(phi) / ( amp * actuationScale) );
      
    } /* for i < numlines */

  goto success;
 failed:
  LALDestroyParsedDataFile(status->statusPtr, &fileContents);
  LALFree (data);
  LALFree (ret);
  ABORT (status, MAKEFAKEDATAC_EREADFILE, MAKEFAKEDATAC_MSGEREADFILE);
      
 success:

  TRY ( LALDestroyParsedDataFile(status->statusPtr, &fileContents), status);

  ret->data = data;
  (*transfer) = ret;
  
  
  DETATCHSTATUSPTR (status);
  RETURN(status);

} /* ReadActuationFunction() */

/** Return a vector of SFTs containg only the bins in [fmin, fmin+Band].
 */ 
void
LALExtractSFTBand ( LALStatus *status, SFTVector **outSFTs, const SFTVector *inSFTs, REAL8 fmin, REAL8 Band )
{
  UINT4 firstBin, numBins, numSFTs;
  UINT4 i;
  REAL8 SFTf0, SFTBand, df;
  SFTVector *ret = NULL;

  INITSTATUS (status, "LALExtractSFTBand", rcsid);
  ATTATCHSTATUSPTR (status);

  ASSERT ( inSFTs, status,  MAKEFAKEDATAC_EBAD,  MAKEFAKEDATAC_MSGEBAD );
  ASSERT ( inSFTs->data[0].data , status,  MAKEFAKEDATAC_EBAD,  MAKEFAKEDATAC_MSGEBAD );

  ASSERT ( outSFTs, status,  MAKEFAKEDATAC_EBAD,  MAKEFAKEDATAC_MSGEBAD );
  ASSERT ( *outSFTs == NULL, status,  MAKEFAKEDATAC_EBAD,  MAKEFAKEDATAC_MSGEBAD );

  numSFTs = inSFTs->length;
  SFTf0 = inSFTs->data[0].f0;
  df = inSFTs->data[0].deltaF;
  SFTBand = df * inSFTs->data[0].data->length;


  if ( (fmin < SFTf0) || ( fmin + Band > SFTf0 + SFTBand ) )
    {
      LALPrintError ( "ERROR: requested frequency-band is not contained in the given SFTs.");
      ABORT ( status,  MAKEFAKEDATAC_EBAD,  MAKEFAKEDATAC_MSGEBAD );
    }

  firstBin = floor ( fmin / df + 0.5 );
  numBins =  floor ( Band / df + 0.5 ) + 1;

  TRY ( LALCreateSFTVector ( status->statusPtr, &ret, numSFTs, numBins ), status );

  for (i=0; i < numSFTs; i ++ )
    {
      SFTtype *dest = &(ret->data[i]);
      SFTtype *src =  &(inSFTs->data[i]);
      COMPLEX8Vector *ptr = dest->data;

      /* copy complete header first */
      memcpy ( dest, src, sizeof(*dest) );
      /* restore data-pointer */
      dest->data = ptr;
      /* set correct fmin */
      dest->f0 = firstBin * df ;

      /* copy the relevant part of the data */
      memcpy ( dest->data->data, src->data->data + firstBin, numBins * sizeof( dest->data->data[0] ) );

    } /* for i < numSFTs */

  /* return final SFT-vector */
  (*outSFTs) = ret;

  DETATCHSTATUSPTR ( status );
  RETURN ( status );

} /* LALExtractSFTBand() */

/* determine if the given filepath is an existing directory or not */
BOOLEAN
is_directory ( const CHAR *fname )
{
  struct stat stat_buf;

  if ( stat (fname, &stat_buf) ) 
    return FALSE;

  if ( ! S_ISDIR (stat_buf.st_mode) )
    return FALSE;
  else
    return TRUE;

} /* is_directory() */

