#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <inttypes.h>
#include "../../autoconf.h"
#include "../../pixel.h"
#include "../../util.h"
#include "../../scrolltext/scrolltext.h"
#include "variants.h"
#include "piece.h"
#include "playfield.h"
#include "view.h"

#define WAIT(ms) wait(ms)

/**
 * \defgroup TetrisViewDefinesPrivate View: Internal constants
 */
/*@{*/

/***********
 * defines *
 ***********/

/** how often should the border blink (to indicate level up) */
#define TETRIS_VIEW_BORDER_BLINK_COUNT 2
/** amount of time (in ms) between border color changes */
#define TETRIS_VIEW_BORDER_BLINK_DELAY 100

/** how often should the lines blink when they get removed */
#define TETRIS_VIEW_LINE_BLINK_COUNT 3
/** amount of time (in ms) between line color changes */
#define TETRIS_VIEW_LINE_BLINK_DELAY 75

/** color of space */
#define TETRIS_VIEW_COLORSPACE   0
/** color of border */
#define TETRIS_VIEW_COLORBORDER  1
/** color of fading lines */
#define TETRIS_VIEW_COLORFADE    2
/** color of a piece */
#define TETRIS_VIEW_COLORPIECE   3
/** color of pause mode */
#define TETRIS_VIEW_COLORPAUSE   1
/** color of line counter */
#define TETRIS_VIEW_COLORCOUNTER 2


#ifdef GAME_TETRIS_FP
	#if NUM_ROWS < NUM_COLS
		#define VIEWCOLS NUM_ROWS
		#define VIEWROWS NUM_ROWS
	#elif NUM_ROWS > NUM_COLS
		#define VIEWCOLS NUM_COLS
		#define VIEWROWS NUM_COLS
	#else
		#define VIEWCOLS NUM_COLS
		#define VIEWROWS NUM_ROWS
	#endif
#else
	#define VIEWCOLS NUM_COLS
	#define VIEWROWS NUM_ROWS
#endif


#if VIEWROWS >= 20
	#define TETRIS_VIEW_YOFFSET_DUMP         ((VIEWROWS - 20) / 2)
	#define TETRIS_VIEW_HEIGHT_DUMP          20
#else
	#define TETRIS_VIEW_YOFFSET_DUMP         0
	#define TETRIS_VIEW_HEIGHT_DUMP          VIEWROWS
#endif


#if VIEWCOLS >= 16
	#define TETRIS_VIEW_XOFFSET_DUMP         (((VIEWCOLS - 16) / 2) + 1)
	#define TETRIS_VIEW_WIDTH_DUMP           10

	#if VIEWROWS >= 16
		#define TETRIS_VIEW_XOFFSET_COUNTER \
				(TETRIS_VIEW_XOFFSET_DUMP + TETRIS_VIEW_WIDTH_DUMP + 1)
		#define TETRIS_VIEW_YOFFSET_COUNT100 ((VIEWCOLS - 14) / 2)
		#define TETRIS_VIEW_YOFFSET_COUNT10  (TETRIS_VIEW_YOFFSET_COUNT100 + 2)
		#define TETRIS_VIEW_YOFFSET_COUNT1   (TETRIS_VIEW_YOFFSET_COUNT10 + 4)

		#define TETRIS_VIEW_XOFFSET_PREVIEW \
				(TETRIS_VIEW_XOFFSET_DUMP + TETRIS_VIEW_WIDTH_DUMP + 1)
		#define TETRIS_VIEW_YOFFSET_PREVIEW (TETRIS_VIEW_YOFFSET_COUNT1 + 4)
	#elif VIEWROWS < 16 && VIEWROWS >= 4
		#define TETRIS_VIEW_XOFFSET_PREVIEW \
				(TETRIS_VIEW_XOFFSET_DUMP + TETRIS_VIEW_WIDTH_DUMP + 1)
		#define TETRIS_VIEW_YOFFSET_PREVIEW  ((VIEWROWS - 4) / 2)
	#endif
#elif (VIEWCOLS < 16) && (VIEWCOLS >= 12)
	#define TETRIS_VIEW_XOFFSET_DUMP         ((VIEWCOLS - 10) / 2)
	#define TETRIS_VIEW_WIDTH_DUMP           10
#elif VIEWCOLS == 11
	#define TETRIS_VIEW_XOFFSET_DUMP         1
	#define TETRIS_VIEW_WIDTH_DUMP           10
#else
	#define TETRIS_VIEW_XOFFSET_DUMP         0
	#define TETRIS_VIEW_WIDTH_DUMP           VIEWCOLS
#endif


/*@}*/


/**
 * \defgroup TetrisViewNoInterface View: Internal non-interface functions
 */
/*@{*/

/***************************
 * non-interface functions *
 ***************************/

/**
 * setpixel replacement which may transform the pixel coordinates
 * @param pV pointer to the view we want to draw on
 * @param x x-coordinate of the pixel
 * @param y y-coordinate of the pixel
 * @param nColor Color of the pixel
 */
void tetris_view_setpixel(tetris_orientation_t nOrientation,
                          uint8_t x,
                          uint8_t y,
                          uint8_t nColor)
{
	x = VIEWCOLS - 1 - x;

	switch (nOrientation)
	{
	case TETRIS_ORIENTATION_0:
		setpixel((pixel){x, y}, nColor);
		break;
	case TETRIS_ORIENTATION_90:
		setpixel((pixel){y, VIEWCOLS - 1 - x}, nColor);
		break;
	case TETRIS_ORIENTATION_180:
		setpixel((pixel){VIEWCOLS - 1 - x, VIEWROWS - 1 - y}, nColor);
		break;
	case TETRIS_ORIENTATION_270:
		setpixel((pixel){VIEWROWS - 1 - y, x}, nColor);
		break;
	}
}


/**
 * draws a horizontal line
 * @param nOrient orientation of the view
 * @param x1 first x-coordinate of the line
 * @param x2 second x-coordinate of the line
 * @param y y-coordinate of the line
 * @param nColor Color of the line
 */
void tetris_view_drawHLine(tetris_orientation_t nOrient,
                           uint8_t x1,
                           uint8_t x2,
                           uint8_t y,
                           uint8_t nColor)
{
	assert(x1 <= x2);

	for (uint8_t x = x1; x <= x2; ++x)
	{
		tetris_view_setpixel(nOrient, x, y, nColor);
	}
}


/**
 * draws a vertical line
 * @param nOrient orientation of the view
 * @param x x-coordinate of the line
 * @param y1 first y-coordinate of the line
 * @param y2 second y-coordinate of the line
 * @param nColor Color of the line
 */
void tetris_view_drawVLine(tetris_orientation_t nOrient,
                           uint8_t x,
                           uint8_t y1,
                           uint8_t y2,
                           uint8_t nColor)
{
	assert(y1 <= y2);

	for (uint8_t y = y1; y <= y2; ++y)
	{
		tetris_view_setpixel(nOrient, x, y, nColor);
	}
}


/**
 * helper function to dim the piece color if game is paused
 * @param pV pointer to the view whose pause status is of interest
 */
uint8_t tetris_view_getPieceColor(tetris_view_t *pV)
{
	if (pV->modeCurrent == TETRIS_VIMO_RUNNING)
	{
		return TETRIS_VIEW_COLORPIECE;
	}
	else
	{
		return TETRIS_VIEW_COLORPAUSE;
	}
}


/**
 * redraws the dump and the falling piece (if necessary)
 * @param pV pointer to the view on which the dump should be drawn
 */
void tetris_view_drawDump(tetris_view_t *pV)
{
	assert(pV->pPl != NULL);
	if (tetris_playfield_getRow(pV->pPl) <= -4)
	{
		return;
	}

	tetris_orientation_t nOrient =
			pV->pVariantMethods->getOrientation(pV->pVariant);

	int8_t nPieceRow = tetris_playfield_getRow(pV->pPl);
	uint16_t nRowMap;
	uint16_t nElementMask;

	tetris_playfield_status_t status = tetris_playfield_getStatus(pV->pPl);
	for (int8_t nRow = TETRIS_VIEW_HEIGHT_DUMP - 1; nRow >= 0; --nRow)
	{
		nRowMap = tetris_playfield_getDumpRow(pV->pPl, nRow);

		// if a piece is hovering or gliding it needs to be drawn
		if ((status == TETRIS_PFS_HOVERING) || (status == TETRIS_PFS_GLIDING) ||
			(status == TETRIS_PFS_GAMEOVER))
		{
			if ((nRow >= nPieceRow) && (nRow <= nPieceRow + 3))
			{
				int8_t y = nRow - nPieceRow;
				int8_t nColumn = tetris_playfield_getColumn(pV->pPl);
				uint16_t nPieceMap =
					tetris_piece_getBitmap(tetris_playfield_getPiece(pV->pPl));
				// clear all bits of the piece we are not interested in and
				// align the remaining row to LSB
				nPieceMap = (nPieceMap & (0x000F << (y << 2))) >> (y << 2);
				// shift remaining part to current column
				if (nColumn >= 0)
				{
					nPieceMap <<= nColumn;
				}
				else
				{
					nPieceMap >>= -nColumn;
				}
				// cut off unwanted stuff
				// nPieceMap &= 0x03ff;
				// finally embed piece into the view
				nRowMap |= nPieceMap;
			}
		}

		nElementMask = 0x0001;

		for (int8_t x = 0; x < TETRIS_VIEW_WIDTH_DUMP; ++x)
		{
			unsigned char nColor;
			if ((nRowMap & nElementMask) != 0)
			{
				nColor = tetris_view_getPieceColor(pV);
			}
			else
			{
				nColor = TETRIS_VIEW_COLORSPACE;
			}
			tetris_view_setpixel(nOrient, TETRIS_VIEW_XOFFSET_DUMP + x,
					TETRIS_VIEW_YOFFSET_DUMP + nRow, nColor);
			nElementMask <<= 1;
		}
	}
}

#ifdef TETRIS_VIEW_XOFFSET_PREVIEW
/**
 * redraws the preview window
 * @param pV pointer to the view on which the piece should be drawn
 * @param pPc pointer to the piece for the preview window (may be NULL)
 */
void tetris_view_drawPreviewPiece(tetris_view_t *pV, tetris_piece_t *pPc)
{
	tetris_orientation_t nOrient =
			pV->pVariantMethods->getOrientation(pV->pVariant);

	if (pPc != NULL)
	{
		uint8_t nColor;
		uint16_t nElementMask = 0x0001;
		uint16_t nPieceMap;
		if (pV->modeCurrent == TETRIS_VIMO_RUNNING)
		{
			nPieceMap = tetris_piece_getBitmap(pPc);
		}
		else
		{
			// an iconized "P"
			nPieceMap = 0x26a6;
		}

		for (uint8_t y = 0; y < 4; ++y)
		{
			for (uint8_t x = 0; x < 4; ++x)
			{
				if ((nPieceMap & nElementMask) != 0)
				{
					nColor = TETRIS_VIEW_COLORPIECE;
				}
				else
				{
					nColor = TETRIS_VIEW_COLORSPACE;
				}
				tetris_view_setpixel(nOrient,
						TETRIS_VIEW_XOFFSET_PREVIEW + x,
						TETRIS_VIEW_YOFFSET_PREVIEW + y,
						nColor);
				nElementMask <<= 1;
			}
		}
	}
	else
	{
		for (uint8_t y = 0; y < 4; ++y)
		{
			for (uint8_t x = 0; x < 4; ++x)
			{
				tetris_view_setpixel(nOrient,
						TETRIS_VIEW_XOFFSET_PREVIEW + x,
						TETRIS_VIEW_YOFFSET_PREVIEW + y,
						TETRIS_VIEW_COLORSPACE);
			}
		}
	}
}
#endif

/**
 * draws borders in the given color
 * @param pV pointer to the view on which the borders should be drawn
 * @param nColor the color for the border
 */
void tetris_view_drawBorders(tetris_view_t *pV,
                             uint8_t nColor)
{
	tetris_orientation_t nOrient =
			pV->pVariantMethods->getOrientation(pV->pVariant);

#if TETRIS_VIEW_YOFFSET_DUMP != 0
	// fill upper space if required
	for (uint8_t y = 0; y < TETRIS_VIEW_YOFFSET_DUMP; ++y)
	{
		tetris_view_drawHLine(nOrient, 0, VIEWCOLS - 1, y, nColor);
	}
#endif

#if VIEWROWS > TETRIS_VIEW_HEIGHT_DUMP
	// fill lower space if required
	uint8_t y = TETRIS_VIEW_YOFFSET_DUMP + TETRIS_VIEW_HEIGHT_DUMP;
	for (; y < VIEWROWS; ++y)
	{
		tetris_view_drawHLine(nOrient, 0, VIEWCOLS - 1, y, nColor);
	}
#endif

#if	TETRIS_VIEW_XOFFSET_DUMP != 0
	// fill left space if required
	for (uint8_t x = 0; x < TETRIS_VIEW_XOFFSET_DUMP; ++x)
	{
		tetris_view_drawVLine(nOrient, x, TETRIS_VIEW_YOFFSET_DUMP,
				TETRIS_VIEW_YOFFSET_DUMP + TETRIS_VIEW_HEIGHT_DUMP - 1, nColor);
	}
#endif

#if VIEWCOLS > 16
	// fill right space if required
	uint8_t x = TETRIS_VIEW_XOFFSET_DUMP + TETRIS_VIEW_WIDTH_DUMP + 5;
	for (; x < VIEWCOLS; ++x)
	{
		tetris_view_drawVLine(nOrient, x, TETRIS_VIEW_YOFFSET_DUMP,
				TETRIS_VIEW_YOFFSET_DUMP + TETRIS_VIEW_HEIGHT_DUMP - 1, nColor);
	}
#endif


#ifdef TETRIS_VIEW_XOFFSET_COUNTER
	tetris_view_drawVLine(nOrient, TETRIS_VIEW_XOFFSET_COUNTER - 1,
			TETRIS_VIEW_YOFFSET_DUMP,
			TETRIS_VIEW_YOFFSET_DUMP + TETRIS_VIEW_HEIGHT_DUMP - 1, nColor);

	for (uint8_t x = TETRIS_VIEW_XOFFSET_COUNTER;
			x < TETRIS_VIEW_XOFFSET_COUNTER + 3; ++x)
	{
		tetris_view_drawVLine(nOrient, x, TETRIS_VIEW_YOFFSET_DUMP,
				TETRIS_VIEW_YOFFSET_COUNT100 - 1, nColor);
		tetris_view_drawVLine(nOrient, x, TETRIS_VIEW_YOFFSET_PREVIEW + 4,
				TETRIS_VIEW_YOFFSET_DUMP + TETRIS_VIEW_HEIGHT_DUMP - 1, nColor);
	}

	tetris_view_drawVLine(nOrient, TETRIS_VIEW_XOFFSET_COUNTER + 3,
			TETRIS_VIEW_YOFFSET_DUMP, TETRIS_VIEW_YOFFSET_COUNT1 + 3, nColor);

	tetris_view_drawVLine(nOrient, TETRIS_VIEW_XOFFSET_COUNTER + 3,
			TETRIS_VIEW_YOFFSET_PREVIEW + 4,
			TETRIS_VIEW_YOFFSET_DUMP + TETRIS_VIEW_HEIGHT_DUMP - 1, nColor);

	tetris_view_drawHLine(nOrient, TETRIS_VIEW_XOFFSET_COUNTER,
			TETRIS_VIEW_XOFFSET_COUNTER + 3, TETRIS_VIEW_YOFFSET_COUNT100 + 1,
			nColor);

	tetris_view_drawHLine(nOrient, TETRIS_VIEW_XOFFSET_COUNTER,
			TETRIS_VIEW_XOFFSET_COUNTER + 3, TETRIS_VIEW_YOFFSET_COUNT10 + 3,
			nColor);

	tetris_view_drawHLine(nOrient, TETRIS_VIEW_XOFFSET_COUNTER,
			TETRIS_VIEW_XOFFSET_COUNTER + 3, TETRIS_VIEW_YOFFSET_COUNT1 + 3,
			nColor);
#elif defined TETRIS_VIEW_XOFFSET_PREVIEW
	tetris_view_drawVLine(nOrient, TETRIS_VIEW_XOFFSET_PREVIEW - 1,
			TETRIS_VIEW_YOFFSET_DUMP,
			TETRIS_VIEW_YOFFSET_DUMP + TETRIS_VIEW_HEIGHT_DUMP - 1, nColor);

	for (uint8_t x = TETRIS_VIEW_XOFFSET_PREVIEW;
			x < TETRIS_VIEW_XOFFSET_PREVIEW + 4; ++x)
	{
		tetris_view_drawVLine(nOrient, x, TETRIS_VIEW_YOFFSET_DUMP,
				TETRIS_VIEW_YOFFSET_PREVIEW - 1, nColor);
		tetris_view_drawVLine(nOrient, x, TETRIS_VIEW_YOFFSET_PREVIEW + 4,
				TETRIS_VIEW_YOFFSET_DUMP + TETRIS_VIEW_HEIGHT_DUMP - 1, nColor);
	}
#elif TETRIS_VIEW_WIDTH_DUMP < VIEWCOLS
	for (uint8_t x = TETRIS_VIEW_XOFFSET_DUMP + TETRIS_VIEW_WIDTH_DUMP;
			x < VIEWCOLS; ++x)
	{
		tetris_view_drawVLine(nOrient, x, TETRIS_VIEW_YOFFSET_DUMP,
				TETRIS_VIEW_YOFFSET_DUMP + TETRIS_VIEW_HEIGHT_DUMP - 1, nColor);
	}
#endif
}


/**
 * lets the borders blink to notify player of a level change
 * @param pV pointer to the view whose borders should blink
 */
void tetris_view_blinkBorders(tetris_view_t *pV)
{
	for (uint8_t i = 0; i < TETRIS_VIEW_BORDER_BLINK_COUNT; ++i)
	{
		tetris_view_drawBorders(pV, TETRIS_VIEW_COLORPIECE);
		WAIT(TETRIS_VIEW_BORDER_BLINK_DELAY);
		tetris_view_drawBorders(pV, TETRIS_VIEW_COLORBORDER);
		WAIT(TETRIS_VIEW_BORDER_BLINK_DELAY);
	}
}


/**
 * lets complete lines blink to emphasize their removal
 * @param pPl pointer to the view whose complete lines should blink
 */
void tetris_view_blinkLines(tetris_view_t *pV)
{

	// reduce necessity of pointer arithmetic
	int8_t nRow = tetris_playfield_getRow(pV->pPl);
	uint8_t nRowMask = tetris_playfield_getRowMask(pV->pPl);

	tetris_orientation_t nOrient =
			pV->pVariantMethods->getOrientation(pV->pVariant);

	// don't try to draw below the border
	int8_t nDeepestRowOffset = ((nRow + 3) < TETRIS_VIEW_HEIGHT_DUMP ?
			3 : TETRIS_VIEW_HEIGHT_DUMP - (nRow + 1));

	// this loop controls how often the lines should blink
	for (uint8_t i = 0; i < TETRIS_VIEW_LINE_BLINK_COUNT; ++i)
	{
		// this loop determines the color of the line to be drawn
		for (uint8_t nColIdx = 0; nColIdx < 2; ++nColIdx)
		{
			// iterate through the possibly complete lines
			for (uint8_t j = 0; j <= nDeepestRowOffset; ++j)
			{
				// is current line a complete line?
				if ((nRowMask & (0x01 << j)) != 0)
				{
					// draw line in current color
					uint8_t y = nRow + j;
					for (uint8_t x = 0; x < 10; ++x)
					{

						uint8_t nColor = (nColIdx == 0 ? TETRIS_VIEW_COLORFADE
								: TETRIS_VIEW_COLORPIECE);
						// setpixel((pixel){14 - x, y}, nColor);
						tetris_view_setpixel(nOrient,
								TETRIS_VIEW_XOFFSET_DUMP + x,
								TETRIS_VIEW_YOFFSET_DUMP + y,
								nColor);
					}
				}
			}
			// wait a few ms to make the blink effect visible
			WAIT(TETRIS_VIEW_LINE_BLINK_DELAY);
		}
	}
}


#ifdef TETRIS_VIEW_XOFFSET_COUNTER
/**
 * displays completed Lines (0-99)
 * @param pV pointer to the view
 */
void tetris_view_showLineNumbers(tetris_view_t *pV)
{

	tetris_orientation_t nOrient =
			pV->pVariantMethods->getOrientation(pV->pVariant);

	// get number of completed lines
	uint16_t nLines = pV->pVariantMethods->getLines(pV->pVariant);

	// get decimal places
	int8_t nOnes = nLines % 10;
	int8_t nTens = (nLines / 10) % 10;
	int8_t nHundreds = (nLines / 100) % 10;

	// draws the decimal places as 3x3 squares with 9 pixels
	for (int i = 0, x = 0, y = 0; i < 9; ++i)
	{
		// pick drawing color for the ones
		uint8_t nOnesPen = nOnes > i ?
			TETRIS_VIEW_COLORCOUNTER : TETRIS_VIEW_COLORSPACE;
		tetris_view_setpixel(nOrient, TETRIS_VIEW_XOFFSET_COUNTER + x,
				TETRIS_VIEW_YOFFSET_COUNT1 + y, nOnesPen);

		// pick drawing color for the tens
		uint8_t nTensPen = nTens > i ?
			TETRIS_VIEW_COLORCOUNTER : TETRIS_VIEW_COLORSPACE;
		tetris_view_setpixel(nOrient, TETRIS_VIEW_XOFFSET_COUNTER + x,
				TETRIS_VIEW_YOFFSET_COUNT10 + y, nTensPen);

		// a maximum of 399 lines can be displayed
		if (i < 3)
		{
			// pick drawing color for the hundreds
			uint8_t nHundredsPen = nHundreds > i ?
				TETRIS_VIEW_COLORCOUNTER : TETRIS_VIEW_COLORSPACE;
			tetris_view_setpixel(nOrient, TETRIS_VIEW_XOFFSET_COUNTER + x,
					TETRIS_VIEW_YOFFSET_COUNT100 + y, nHundredsPen);

		}

		// wrap lines if required
		if ((++x % 3) == 0)
		{
			++y;
			x = 0;
		}
	}
}
#endif


/**
 * unpacks the champion's initials from the uint16_t packed form
 * @param nHighscoreName the champion's initials packed into a uint16_t
 * @param pszName pointer to an array of char for the unpacked initials
 */
void tetris_view_formatHighscoreName(uint16_t nHighscoreName,
                                     char *pszName)
{
	pszName[0] = ((nHighscoreName >> 10) & 0x1F) + 65;
	if (pszName[0] == '_')
	{
		pszName[0] = ' ';
	}

	pszName[1] = ((nHighscoreName >> 5) & 0x1F) + 65;
	if (pszName[1] == '_')
	{
		pszName[1] = ' ';
	}

	pszName[2] = (nHighscoreName & 0x1F) + 65;
	if (pszName[2] == '_')
	{
		pszName[2] = ' ';
	}

	pszName[3] = '\0';
}
/*@}*/


/****************************
 * construction/destruction *
 ****************************/

tetris_view_t *tetris_view_construct(const tetris_variant_t *const pVarMethods,
                                     void *pVariantData,
                                     tetris_playfield_t *pPl)
{
	// memory allocation
	assert((pVariantData != NULL) && (pPl != NULL));
	tetris_view_t *pView =
		(tetris_view_t *) malloc(sizeof(tetris_view_t));
	assert(pView != NULL);

	// init
	memset(pView, 0, sizeof(tetris_view_t));
	pView->pVariantMethods = pVarMethods;
	pView->pVariant = pVariantData;
	pView->pPl = pPl;
	pView->modeCurrent = pView->modeOld = TETRIS_VIMO_RUNNING;

	// drawing some first stuff
	clear_screen(0);
	tetris_view_drawBorders(pView, TETRIS_VIEW_COLORBORDER);

	return pView;
}


void tetris_view_destruct(tetris_view_t *pView)
{
	assert(pView != NULL);
	free(pView);
}


/***************************
 *  view related functions *
 ***************************/

void tetris_view_getDimensions(int8_t *w,
                               int8_t *h)
{
	assert((w != NULL) && (h != NULL));
	*w = TETRIS_VIEW_WIDTH_DUMP;
	*h = TETRIS_VIEW_HEIGHT_DUMP;
}


void tetris_view_setViewMode(tetris_view_t *pV, tetris_view_mode_t vm)
{
	pV->modeOld = pV->modeCurrent;
	pV->modeCurrent = vm;
}


void tetris_view_update(tetris_view_t *pV)
{
	assert(pV != NULL);

	tetris_view_drawBorders(pV, TETRIS_VIEW_COLORBORDER);

#ifdef TETRIS_VIEW_XOFFSET_PREVIEW
	// draw preview piece
	tetris_view_drawPreviewPiece(pV,
			pV->pVariantMethods->getPreviewPiece(pV->pVariant));
#endif

	// let complete lines blink (if there are any)
	if (tetris_playfield_getRowMask(pV->pPl) != 0)
	{
		tetris_view_blinkLines(pV);
	}

#ifdef TETRIS_VIEW_XOFFSET_COUNTER
	// update line counter
	tetris_view_showLineNumbers(pV);
#endif

	// draw dump
	tetris_view_drawDump(pV);

	// visual feedback to inform about a level change
	uint8_t nLevel = pV->pVariantMethods->getLevel(pV->pVariant);
	if (nLevel != pV->nOldLevel)
	{
		tetris_view_blinkBorders(pV);
		pV->nOldLevel = nLevel;
	}
	
}


void tetris_view_showResults(tetris_view_t *pV)
{
#ifdef SCROLLTEXT_SUPPORT
	char pszResults[54], pszHighscoreName[4];
	uint16_t nScore = pV->pVariantMethods->getScore(pV->pVariant);
	uint16_t nHighscore = pV->pVariantMethods->getHighscore(pV->pVariant);
	uint16_t nLines = pV->pVariantMethods->getLines(pV->pVariant);
	uint16_t nHighscoreName =
			pV->pVariantMethods->getHighscoreName(pV->pVariant);
	tetris_view_formatHighscoreName(nHighscoreName, pszHighscoreName);

	if (nScore <= nHighscore)
	{
		snprintf(pszResults, sizeof(pszResults),
			"</#Lines %u    Score %u    Highscore %u (%s)",
			nLines, nScore, nHighscore, pszHighscoreName);
	}
	else
	{
		snprintf(pszResults, sizeof(pszResults),
			"</#Lines %u    New Highscore %u", nLines, nScore);
	}
	scrolltext(pszResults);
#endif
}
