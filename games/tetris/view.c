#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <inttypes.h>
#include "../../config.h"
#include "../../pixel.h"
#include "../../util.h"
#include "../../scrolltext/scrolltext.h"
#include "logic.h"
#include "piece.h"
#include "playfield.h"
#include "view.h"

#define WAIT(ms) wait(ms)

#ifdef GAME_TETRIS_FP
uint8_t tetris_screendir;
#endif

void (*setpixel_wrapper)(pixel p, unsigned char value);


/***********
 * defines *
 ***********/

// how often should the border blink (to indicate level up)
#define TETRIS_VIEW_BORDER_BLINK_COUNT 2
// amount of time (in ms) between border color changes
#define TETRIS_VIEW_BORDER_BLINK_DELAY 100

// how often should the lines blink when they get removed
#define TETRIS_VIEW_LINE_BLINK_COUNT 3
// amount of time (in ms) between line color changes
#define TETRIS_VIEW_LINE_BLINK_DELAY 75

// colors of game elements
#define TETRIS_VIEW_COLORSPACE   0
#define TETRIS_VIEW_COLORBORDER  1
#define TETRIS_VIEW_COLORFADE    2
#define TETRIS_VIEW_COLORPIECE   3
#define TETRIS_VIEW_COLORPAUSE   1
#define TETRIS_VIEW_COLORCOUNTER 2





/***************************
 * non-interface functions *
 ***************************/

/* Function:      tetris_view_getPieceColor
 * Description:   helper function to dim the piece color if game is paused
 * Argument pV:   pointer to the view whose pause status is of interest
 * Return value:  void
 */
uint8_t tetris_view_getPieceColor (tetris_view_t *pV)
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


/* Function:      tetris_view_drawDump
 * Description:   redraws the dump and the falling piece (if necessary)
 * Argument pV:   pointer to the view on which the dump should be drawn
 * Return value:  void
 */
void tetris_view_drawDump(tetris_view_t *pV)
{
	assert(pV->pPl != NULL);
	if (tetris_playfield_getRow(pV->pPl) <= -4)
	{
		return;
	}

	int8_t nPieceRow = tetris_playfield_getRow(pV->pPl);

	// only redraw dump completely if the view mode has been changed
	int8_t nStartRow;
	if (pV->modeCurrent == pV->modeOld)
	{
		nStartRow = ((nPieceRow + 3) < 16) ? (nPieceRow + 3) : 15;
	}
	else
	{
		nStartRow = 15;
	}

	uint16_t nRowMap;
	uint16_t nElementMask;

	tetris_playfield_status_t status = tetris_playfield_getStatus(pV->pPl);
	for (int8_t nRow = nStartRow; nRow >= 0; --nRow)
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
				nPieceMap &= 0x03ff;
				// finally embed piece into the view
				nRowMap |= nPieceMap;
			}
		}

		nElementMask = 0x0001;

		for (int8_t x = 0; x < 10; ++x)
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
			setpixel_wrapper((pixel){14-x,nRow}, nColor);
			nElementMask <<= 1;
		}
	}
}


/* Function:      tetris_view_drawPreviewPiece
 * Description:   redraws the preview window
 * Argument pV:   pointer to the view on which the piece should be drawn
 * Argmument pPc: pointer to the piece for the preview window (may be NULL)
 * Return value:  void
 */
void tetris_view_drawPreviewPiece(tetris_view_t *pV, tetris_piece_t *pPc)
{
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
				setpixel_wrapper((pixel) {3 - x, y + 6}, nColor);
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
				setpixel_wrapper((pixel) {3 - x, y + 6}, TETRIS_VIEW_COLORSPACE);
			}
		}
	}
}


/* Function:         tetris_view_drawBorders
 * Description:      draws borders in the given color
 * Argument nColor:  the color for the border
 * Return value:     void
 */
void tetris_view_drawBorders(uint8_t nColor)
{
	// drawing playfield
	uint8_t x, y;
	for (y = 0; y < 16; ++y)
	{
		setpixel_wrapper((pixel){4, y}, nColor);
		setpixel_wrapper((pixel){15, y}, nColor);
	}

	for (y = 0; y < 5; ++y)
	{
		for (x = 0; x <= 3; ++x)
		{
			if ((y < 1 || y > 3) || (x < 1 || y > 3))
			{
				setpixel_wrapper((pixel){x, y}, nColor);
				setpixel_wrapper((pixel){x, y + 11}, nColor);
			}
		}
	}
}


/* Function:     tetris_view_blinkBorders
 * Description:  lets the borders blink to notify player of a level change
 * Return value: void
 */
void tetris_view_blinkBorders()
{
	for (uint8_t i = 0; i < TETRIS_VIEW_BORDER_BLINK_COUNT; ++i)
	{
		tetris_view_drawBorders(TETRIS_VIEW_COLORPIECE);
		WAIT(TETRIS_VIEW_BORDER_BLINK_DELAY);
		tetris_view_drawBorders(TETRIS_VIEW_COLORBORDER);
		WAIT(TETRIS_VIEW_BORDER_BLINK_DELAY);
	}
}


/* Function:      tetris_view_blinkLines
 * Description:   lets complete lines blink to emphasize their removal
 * Argmument pPl: pointer to the playfield whose complete lines should blink
 * Return value:  void
 */


void tetris_view_blinkLines(tetris_playfield_t *pPl)
{
	// reduce necessity of pointer arithmetic
	int8_t nRow = tetris_playfield_getRow(pPl);
	uint8_t nRowMask = tetris_playfield_getRowMask(pPl);

	// don't try to draw below the border
	int8_t nDeepestRowOffset = ((nRow + 3) < tetris_playfield_getHeight(pPl) ?
			3 : tetris_playfield_getHeight(pPl) - (nRow + 1));

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
						setpixel_wrapper((pixel){14 - x, y}, nColor);
					}
				}
			}
			// wait a few ms to make the blink effect visible
			WAIT(TETRIS_VIEW_LINE_BLINK_DELAY);
		}
	}
}


/* Function:        tetris_view_showLineNumbers
 * Description:     displays completed Lines (0-99)
 * Argmument pV:    pointer to the view
 * Return value:    void
 */
void tetris_view_showLineNumbers(tetris_view_t *pV)
{
	// get number of completed lines
	uint16_t nLines = tetris_logic_getLines(pV->pLogic);

	// get decimal places
	int8_t nOnes = nLines % 10;
	int8_t nTens = (nLines / 10) % 10;

	// draws the decimal places as 3x3 squares with 9 pixels
	for (int i = 0, x = 1, y = 1; i < 9; ++i)
	{
		// pick drawing color
		uint8_t nOnesPen = nOnes > i ?
			TETRIS_VIEW_COLORCOUNTER : TETRIS_VIEW_COLORSPACE;
		uint8_t nTensPen = nTens > i ?
			TETRIS_VIEW_COLORCOUNTER : TETRIS_VIEW_COLORSPACE;
		// wrap lines if required
		if ((x % 4) == 0)
		{
			y++;
			x = 1;
		}
		// ones
		setpixel_wrapper((pixel){x, y}, nOnesPen);
		// tens (increment x, add vertical offset for lower part of the border)
		setpixel_wrapper((pixel){x++, y + 11}, nTensPen);
	}
}


/****************************
 * construction/destruction *
 ****************************/

/* Function:     tetris_view_construct
 * Description:  constructs a view for André's borg
 * Argument pPl: pointer to logic object which should be observed
 * Argument pPl: pointer to playfield which should be observed
 * Return value: pointer to a newly created view
 */
tetris_view_t *tetris_view_construct(tetris_logic_t *pLogic,
                                     tetris_playfield_t *pPl,
                                     uint8_t nFirstPerson)
{
	// memory allocation
	assert((pLogic != NULL) && (pPl != NULL));
	tetris_view_t *pView =
		(tetris_view_t *) malloc(sizeof(tetris_view_t));
	assert(pView != NULL);

	// init
	memset(pView, 0, sizeof(tetris_view_t));
	pView->pLogic = pLogic;
	pView->pPl = pPl;
	pView->modeCurrent = TETRIS_VIMO_RUNNING;
	pView->modeOld = TETRIS_VIMO_RUNNING;

#ifdef GAME_TETRIS_FP
	// set setpixel wrapper
	if (nFirstPerson)
		setpixel_wrapper = tetris_view_setpixel_fp;
	else
		setpixel_wrapper = setpixel;
#else
	setpixel_wrapper = setpixel;
#endif

	// drawing some first stuff
	clear_screen(0);
	tetris_view_drawBorders(TETRIS_VIEW_COLORBORDER);

	return pView;
}


/* Function:       tetris_view_destruct
 * Description:    destructs a view
 * Argument pView: pointer to the view which should be destructed
 * Return value:   void
 */
void tetris_view_destruct(tetris_view_t *pView)
{
	assert(pView != NULL);
	free(pView);
}


/***************************
 *  view related functions *
 ***************************/

/* Function:     tetris_view_getDimensions
 * Description:  destructs a view
 * Argument w:   [out] pointer to an int8_t to store the playfield width
 * Argument h:   [out] pointer to an int8_t to store the playfield height
 * Return value: void
 */
void tetris_view_getDimensions(int8_t *w,
                               int8_t *h)
{
	assert((w != NULL) && (h != NULL));
	*w = 10;
	*h = 16;
}


/* Function:     tetris_view_setViewMode
 * Description:  sets the view mode (pause or running)
 * Argument pV:  pointer to the view whose mode should be set
 * Argument vm:  see definition of tetris_view_mode_t
 * Return value: void
 */
void tetris_view_setViewMode(tetris_view_t *pV, tetris_view_mode_t vm)
{
	pV->modeOld = pV->modeCurrent;
	pV->modeCurrent = vm;
}


/* Function:     tetris_view_update
 * Description:  informs a view about changes in the game
 * Argument pV:  pointer to the view which should be updated
 * Return value: void
 */
void tetris_view_update(tetris_view_t *pV)
{
	assert(pV != NULL);

	// let complete lines blink (if there are any)
	if (tetris_playfield_getRowMask(pV->pPl) != 0)
	{
		tetris_view_blinkLines(pV->pPl);
		tetris_view_showLineNumbers(pV);
	}

	// draw preview piece
	tetris_view_drawPreviewPiece(pV, tetris_logic_getPreviewPiece(pV->pLogic));

	// draw dump
	tetris_view_drawDump(pV);

	// visual feedback to inform about a level change
	uint8_t nLevel = tetris_logic_getLevel(pV->pLogic);
	if (nLevel != pV->nOldLevel)
	{
		tetris_view_blinkBorders();
		pV->nOldLevel = nLevel;
	}
	
}


/* Function:                tetris_view_formatHighscoreName
 * Description:             convert uint16_t into ascii Highscore
 *                          (only used internally in view.c)
 * Argument nHighscoreName: packed integer with highscoreName
 * Argument pszName:        pointer to a char array where result is stored
 * Return value:            void
 */
void tetris_view_formatHighscoreName(uint16_t nHighscoreName, char *pszName)
{
  pszName[0] = ((nHighscoreName>>10)&0x1F) + 65;
  if (pszName[0] == '_') pszName[0] = ' ';

  pszName[1] = ((nHighscoreName>> 5)&0x1F) + 65;
  if (pszName[1] == '_') pszName[1] = ' ';

  pszName[2] = ( nHighscoreName     &0x1F) + 65;
  if (pszName[2] == '_') pszName[2] = ' ';

  pszName[3] = 0;
}


/* Function:     tetris_view_showResults
 * Description:  shows results after game
 * Argument pV:  pointer to the view which should show the reults
 * Return value: void
 */
void tetris_view_showResults(tetris_view_t *pV)
{
#ifdef SCROLLTEXT_SUPPORT
	char pszResults[54], pszHighscoreName[4];
	uint16_t nScore = tetris_logic_getScore(pV->pLogic);
	uint16_t nHighscore = tetris_logic_getHighscore(pV->pLogic);
	uint16_t nLines = tetris_logic_getLines(pV->pLogic);

	uint16_t nHighscoreName = tetris_logic_getHighscoreName(pV->pLogic);
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


#ifdef GAME_TETRIS_FP
/* Function:     tetris_view_setpixel_fp
 * Description:  own setpixel wrapper for first person mode
 * Return value: void
 */
void tetris_view_setpixel_fp(pixel p, unsigned char value) {
  switch (tetris_screendir) {
    case 0: setpixel(p,value); break;
    case 1: setpixel((pixel){p.y,15-p.x}, value); break;
    case 2: setpixel((pixel){15-p.x,15-p.y}, value); break;
    case 3: setpixel((pixel){15-p.y,p.x}, value); break;
  }
}

/* Function:     tetris_view_rotate
 * Description:  rotate view for first person mode
 * Return value: void
 */
void tetris_view_rotate(void) {
  unsigned char plane, row, byte, shift, off, sbyte, hrow;
  unsigned char new_pixmap[NUMPLANE][NUM_ROWS][LINEBYTES];

  tetris_screendir = (tetris_screendir+1)%4;
	
// if ( NUM_ROWS != 16 || LINEBYTES != 2 ) return;

  memset(&new_pixmap, 0, sizeof(new_pixmap));
  for(plane=0; plane<NUMPLANE; plane++){
    for(row=0;row<NUM_ROWS; row++){
      for(byte=0;byte<LINEBYTES;byte++){
        hrow = row%8;
	shift = 7-hrow;
	off = ((byte==0)?15:7);
	sbyte = (row<8) ? 1 : 0;
			  
	new_pixmap[plane][row][1-byte] = 
	(
	  ( ((pixmap[plane][off  ][sbyte] >> shift)&1) << 7 ) |
	  ( ((pixmap[plane][off-1][sbyte] >> shift)&1) << 6 ) |
	  ( ((pixmap[plane][off-2][sbyte] >> shift)&1) << 5 ) |
	  ( ((pixmap[plane][off-3][sbyte] >> shift)&1) << 4 ) |
	  ( ((pixmap[plane][off-4][sbyte] >> shift)&1) << 3 ) |
	  ( ((pixmap[plane][off-5][sbyte] >> shift)&1) << 2 ) |
	  ( ((pixmap[plane][off-6][sbyte] >> shift)&1) << 1 ) |
	  ( ((pixmap[plane][off-7][sbyte] >> shift)&1) << 0 )
	);

      }
    }
  }

  memcpy(&pixmap, &new_pixmap, sizeof(pixmap));
}

#endif