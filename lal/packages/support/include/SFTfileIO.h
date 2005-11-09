/*
 * Copyright (C) 2004, 2005 R. Prix, B. Machenschalk, A.M. Sintes
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with with program; see the file COPYING. If not, write to the 
 *  Free Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, 
 *  MA  02111-1307  USA
 */

/** \defgroup SFTfileIO
 * \ingroup support
 * \author R. Prix, B. Machenschalk, A.M. Sintes
 * \date $Date$
 * \brief Module for reading/writing/manipulating SFTs (Short Fourier transforms)
 *
 <p>
 <h3>Usage: Reading and writing of SFT-files</h3>
  
 The basic operation of <b>reading SFTs</b> from files proceeds in two simple steps:

 	-# LALSFTdataFind(): get an '::SFTCatalog' of SFTs matching certain requirements
	-# LALLoadSFTs(): load the desired frequency-band from the SFTs described in the catalogue

 <b>Note 1:</b> currently supported SFT file-formats are (merged or single) SFT-v1 and SFT-v2 files. 
 This might be extended in the future to support further file-formats (frames?). 
 None of the following API depends on the details of the underlying file-format. This will ensure that
 codes using the following functions will NOT have to be changed irrespective of SFT file-format used.

 <b>Note 2:</b> irrespective of the underlying SFT file-format, the returned SFTs (::SFTVector) will 
 <em>ALWAYS</em> be normalized according the the LAL-specification for frequency-series 
 (<tt>LIGO-T010095-00</tt>), that is the pure DFT of the time-series \f$x_j\f$ is <em>multiplied</em> 
 by the time-step \f$\Delta t\f$:
 \f[
 \mathrm{data}[k] = X^\mathrm{d}_k = \Delta t \,\sum_{j=0}^{N-1} x_j \,e^{-i2\pi \,k \,j / N}
 \f]

 <p>
 For <b>writing SFTs</b> there are two functions, depending on the desired output-format (v1 or v2  SFts):
 	- LALWriteSFT2file(): write a single SFT (::SFTtype) into an SFT-file following the specification v2 
	(<tt>LIGO-T040164-01-Z</tt>).

	- void LALWriteSFTfile(): write an ::SFTtype into an SFT-v1 file. This is <b>obsolete</b> and 
	deprecated, and should only be used to produce SFT-files for old codes not using the new API.!

 <h4>Details to 1: find matching SFTs and get the SFTCatalog:</h4>

 \code
 LALSFTdataFind(LALStatus *, SFTCatalog **catalog, const CHAR *file_pattern, SFTConstraints *constraints);
 \endcode

 This function returns an SFTCatalog of matching SFTs for a given file-pattern 
 (e.g. "SFT.*", "SFT.000", "/some/path/some_files_[0-9]?.sft", etc ) and additional, optional SFTConstraints. 

 The optional constraints are:
 - detector-name (e.g. "H1", "H2", "L1", "G1", "V1", etc..)
 - GPS start-time + end-time
 - a list of GPS-timestamps

 Any constraint can be specified as \c NULL, all given constraints will be combined by logical \c AND.
 [ Note: if a timestamps-list is given, *ALL* timestamps within <tt>[startTime, endTime]</tt> MUST be found!]

 The returned SFTCatalog is a vector of 'SFTDescriptor's describing one SFT, with the fields
 - \c locator:  an opaque data-type describing where to read this SFT from.
 - \c header:	the SFts header
 - \c comment: the comment-string found in the SFT, if any
 - \c numBins: the number of frequency-bins in the SFT

 One can use the following catalog-handling API functions:
 - LALDestroySFTCatalog(): free up a complete SFT-catalog
 - LALSFTtimestampsFromCatalog(): extract the list of SFT timestamps found in the ::SFTCatalog
 - LALDestroyTimestampVector(): free up a timestamps-vector (::LIGOTimeGPSVector)
 - XLALshowSFTLocator(): [*debugging only*] show a static string describing the 'locator'

 
 <b>NOTE:</b> The SFTs in the catalogue are sorted in order of increasing GPS-epoch!
 

 <h4>Details to 2: load frequency-band from SFTs described in an SFTCatalog</h4>

 \code
 LALLoadSFTs ( LALStatus *, SFTVector **sfts, const SFTCatalog *catalog, REAL8 fMin, REAL8 fMax);
 \endcode

 This function takes an ::SFTCatalog and reads the smallest frequency-band containing <tt>[fMin, fMax]</tt>
 from the SFTs, returning the resulting ::SFTVector.
 
 The frequency-bounds are optional and \c -1 can be used to specify an 'open bound', i.e.<br>
 <tt>[-1, fMax]</tt>: read from first frequency-bin in the SFT up to \c fMax.<br>
 <tt>[fMin, -1]</tt>: read from \c fMin up to last frequency-bin in the SFTS<br>
 <tt>[-1, -1]</tt>: read ALL frequency-bins from SFT.


 Additional functions for handling ::SFTVector's are:
 - LALDestroySFTVector(): free up a complete SFT-vector
 - LALConcatSFTVectors(): concatenate two ::SFTVector's
 - LALAppendSFT2Vector(): append a single SFT (::SFTtype) to an ::SFTVector

 *
 */
 
/** \file 
 * \ingroup SFTfileIO
 * \date $Date$
 * \brief Header file defining the API for the SFTfileIO modules.
 *
 *
 * Routines for reading and writing SFT binary files.
 * 
 */

#ifndef _SFTFILEIO_H  	/* Double-include protection. */
#define _SFTFILEIO_H

/* includes */
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lal/LALStdlib.h>
#include <lal/LALConstants.h>
#include <lal/AVFactories.h>
#include <lal/SeqFactories.h>
#include <lal/PulsarDataTypes.h>

#ifdef  __cplusplus   /* C++ protection. */
extern "C" {
#endif

NRCSID (SFTFILEIOH, "$Id$");

/** \name Error codes */
/*@{*/
#define SFTFILEIO_ENULL 	1
#define SFTFILEIO_EFILE 	2
#define SFTFILEIO_EHEADER 	3
#define SFTFILEIO_EVERSION 	4
#define SFTFILEIO_EVAL 		5
#define SFTFILEIO_EENDIAN 	6
#define SFTFILEIO_ENONULL 	12
#define SFTFILEIO_EFREQBAND 	13
#define SFTFILEIO_EMEM 		14
#define SFTFILEIO_EGLOB 	15
#define SFTFILEIO_EDIFFLENGTH 	17
#define SFTFILEIO_ESFTFORMAT	18
#define SFTFILEIO_ESFTWRITE	19 
#define SFTFILEIO_ECONSTRAINTS  20

#define SFTFILEIO_MSGENULL 	"Null pointer"
#define SFTFILEIO_MSGEFILE 	"Error in file-IO"
#define SFTFILEIO_MSGEHEADER 	"Incorrect header in file"
#define SFTFILEIO_MSGEVERSION 	"This SFT-version is not currently supported"
#define SFTFILEIO_MSGEVAL  	"Invalid value"
#define SFTFILEIO_MSGEENDIAN 	"Wrong endian encoding of SFT (not supported yet"
#define SFTFILEIO_MSGENONULL  	"Output pointer not NULL"
#define SFTFILEIO_MSGEFREQBAND 	"Required frequency-band is not in SFT"
#define SFTFILEIO_MSGEMEM 	"Out of memory"
#define SFTFILEIO_MSGEGLOB 	"Failed to get filelist from directory/pattern"
#define SFTFILEIO_MSGEDIFFLENGTH "Sorry, can only read SFTs of identical length (currently)"
#define SFTFILEIO_MSGESFTFORMAT	 "Illegal SFT-format"
#define SFTFILEIO_MSGESFTWRITE	 "Failed to write SFT to file"
#define SFTFILEIO_MSGECONSTRAINTS "Could not satisfy the requested SFT-query constraints"

/*@}*/

/** 'Constraints' for SFT-matching: which detector, within which time-stretch and which 
 * timestamps exactly should be loaded ? 
 * Any of the entries is optional, and they will be combined by logical AND.
 * Note however, that *ALL* timestamps within [startTime, endTime] MUST be found if specified.
 */
typedef struct
{
  CHAR *detector;			/**< 2-char channel-prefix describing the detector (eg 'H1', 'H2', 'L1', 'G1' etc) */
  LIGOTimeGPS *startTime;		/**< only include SFTs starting >= startTime */
  LIGOTimeGPS *endTime;			/**< only include SFTs starting <= endTime */
  LIGOTimeGPSVector *timestamps;	/**< list of timestamps  */
} SFTConstraints;


/** A 'descriptor' of an SFT: basically containing the header-info plus an opaque description
 * of where exactly to load this SFT from.
 */
typedef struct 
{
  struct tagSFTLocator *locator; 	/**< *internal* description of where to find this SFT [opaque!] */
  SFTtype header;			/**< SFT-header info */
  CHAR *comment;			/**< comment-entry in SFT-header (v2 only) */
  UINT4 numBins;			/**< number of frequency-bins in this SFT */
} SFTDescriptor;


/** An "SFT-catalogue": a vector of SFTdescriptors, as returned by LALSFTdataFind() */
typedef struct 
{
  UINT4 length;
  SFTDescriptor *data;
} SFTCatalog;

/*
 * Functions Declarations (i.e., prototypes).
 */

/*================================================================================
 * NEW API: allowing for SFT-v2 
 *================================================================================*/
void LALSFTdataFind (LALStatus *, SFTCatalog **catalog, const CHAR *file_pattern, SFTConstraints *constraints);

void LALLoadSFTs ( LALStatus *, SFTVector **sfts, const SFTCatalog *catalog, REAL8 fMin, REAL8 fMax);
void LALWriteSFT2file (LALStatus *, const SFTtype *sft, const CHAR *fname, const CHAR *comment ); 

void LALDestroySFTCatalog ( LALStatus *status, SFTCatalog **catalog );
void LALSFTtimestampsFromCatalog (LALStatus *, LIGOTimeGPSVector **timestamps, const SFTCatalog *catalog );
const CHAR * XLALshowSFTLocator ( const struct tagSFTLocator *locator );

/*================================================================================
 * OBSOLETE v1-only API [DEPRECATED!]
 *================================================================================*/


/** [DEPRECATED] This structure contains the header-info contained in an SFT-file of specification 
 * version v1.0.
 */
typedef struct tagSFTHeader {
  REAL8  version;		/**< SFT version-number (currently only 1.0 allowed )*/
  INT4   gpsSeconds;		/**< gps start-time (seconds)*/
  INT4   gpsNanoSeconds;	/**< gps start-time (nanoseconds) */
  REAL8  timeBase;		/**< length of data-stretch in seconds */
  INT4   fminBinIndex;		/**< first frequency-index contained in SFT */
  INT4   length;  		/**< number of frequency bins */
} SFTHeader;



void LALReadSFTheader (LALStatus *, SFTHeader *header, const CHAR *fname); 
void LALReadSFTdata (LALStatus *, SFTtype *sft, const CHAR *fname, INT4 fminBinIndex);
void LALWriteSFTfile (LALStatus *, const SFTtype *sft, const CHAR *outfname);
void LALReadSFTfile (LALStatus *, SFTtype **sft, REAL8 fMin, REAL8 fMax, const CHAR *fname);

void LALReadSFTfiles (LALStatus *,
		      SFTVector **sftvect, 
		      REAL8 fMin, 
		      REAL8 fMax, 
		      UINT4 wingBins, 
		      const CHAR *fpattern);

void
LALGetSFTheaders (LALStatus *,
		  SFTVector **headers,
		  const CHAR *fpattern,
		  const LIGOTimeGPS *startTime,
		  const LIGOTimeGPS *endTime);


#ifdef  __cplusplus
}                /* Close C++ protection */
#endif

#endif     /* Close double-include protection _SFTBIN_H */
 







