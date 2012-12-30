/*
 * Snes9x - Portable Super Nintendo Entertainment System (TM) emulator.
 *
 * (c) Copyright 1996 - 2001 Gary Henderson (gary.henderson@ntlworld.com) and
 *                           Jerremy Koot (jkoot@snes9x.com)
 *
 * Super FX C emulator code 
 * (c) Copyright 1997 - 1999 Ivar (ivar@snes9x.com) and
 *                           Gary Henderson.
 * Super FX assembler emulator code (c) Copyright 1998 zsKnight and _Demo_.
 *
 * DSP1 emulator code (c) Copyright 1998 Ivar, _Demo_ and Gary Henderson.
 * C4 asm and some C emulation code (c) Copyright 2000 zsKnight and _Demo_.
 * C4 C code (c) Copyright 2001 Gary Henderson (gary.henderson@ntlworld.com).
 *
 * DOS port code contains the works of other authors. See headers in
 * individual files.
 *
 * Snes9x homepage: http://www.snes9x.com
 *
 * Permission to use, copy, modify and distribute Snes9x in both binary and
 * source form, for non-commercial purposes, is hereby granted without fee,
 * providing that this license information and copyright notice appear with
 * all copies and any derived work.
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event shall the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Snes9x is freeware for PERSONAL USE only. Commercial users should
 * seek permission of the copyright holders first. Commercial use includes
 * charging money for Snes9x or software derived from Snes9x.
 *
 * The copyright holders request that bug fixes and improvements to the code
 * should be forwarded to them so everyone can benefit from the modifications
 * in future versions.
 *
 * Super NES and Super Nintendo Entertainment System are trademarks of
 * Nintendo Co., Limited and its subsidiary companies.
 */
#ifdef __linux
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <stdlib.h>
#include <signal.h>

#include <SDL/SDL.h>

#include "snes9x.h"
#include "memmap.h"
#include "debug.h"
#include "ppu.h"
#include "snapshot.h"
#include "gfx.h"
#include "display.h"
#include "apu.h"
#include "keydef.h"

#define COUNT(a) (sizeof(a) / sizeof(a[0]))

SDL_Surface *screen, *gfxscreen;
SDL_Joystick *joy;

uint16 *RGBconvert;
extern uint32 xs, ys, cl, cs;

#ifndef _ZAURUS
int S9xMinCommandLineArgs ()
{
    return (2);
}

void S9xGraphicsMode ()
{
}

void S9xTextMode ()
{
}
#endif

extern uint8 *keyssnes;
void S9xInitDisplay (int /*argc*/, char ** /*argv*/)
{
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | (Settings.NextAPUEnabled ? SDL_INIT_AUDIO : 0)) < 0 ) 
	{
		printf("Could not initialize SDL(%s)\n", SDL_GetError());
		S9xExit();
	}
	atexit(SDL_Quit);
	keyssnes = SDL_GetKeyState(NULL);
	screen = SDL_SetVideoMode(xs, ys, 16, SDL_SWSURFACE);
	SDL_ShowCursor(0); // rPi: we're not really interested in showing a mouse cursor

	joy = SDL_JoystickOpen(0);

	if(joy) {
		printf("Opened joystick 0.\n");
		if(SDL_JoystickEventState(SDL_ENABLE) != SDL_ENABLE) {
			printf("Could not set joystick event state\n", SDL_GetError());
			S9xExit();
		}
	} else {
		printf("Could not initialize joystick.");
	}

	if (screen == NULL)
	{
		printf("Couldn't set video mode: %s\n", SDL_GetError());
		S9xExit();
	}
	if (Settings.SupportHiRes) {
		gfxscreen = SDL_CreateRGBSurface(SDL_SWSURFACE, 512, 480, 16, 0, 0, 0, 0);
		GFX.Screen = (uint8 *)gfxscreen->pixels;
		GFX.Pitch = 512 * 2;
	} else {
		GFX.Screen = (uint8 *)screen->pixels + 64;
		GFX.Pitch = 320 * 2;
	}
	GFX.SubScreen = (uint8 *)malloc(512 * 480 * 2);
	GFX.ZBuffer = (uint8 *)malloc(512 * 480 * 2);
	GFX.SubZBuffer = (uint8 *)malloc(512 * 480 * 2);

	RGBconvert = (uint16 *)malloc(65536 * 2);
	if (!RGBconvert)
	{
//		OutOfMemory();
		S9xExit();
	}
	for (uint32 i = 0; i < 65536; i++) 
		((uint16 *)(RGBconvert))[i] = ((i >> 11) << 10) | ((((i >> 5) & 63) >> 1) << 5) | (i & 31);
}

void S9xDeinitDisplay ()
{
//	SDL_FreeSurface(gfxscreen);
	SDL_FreeSurface(screen);

	if(SDL_JoystickOpened(0))
		SDL_JoystickClose(joy); // Should this go here? WHO KNOWS

	free(GFX.SubScreen);
	free(GFX.ZBuffer);
	free(GFX.SubZBuffer);
}

void S9xSetPalette ()
{
}

void S9xSetTitle (const char * /*title*/)
{
}

#ifndef _ZAURUS
const char *S9xSelectFilename (const char *def, const char *dir1,
			    const char *ext1, const char *title)
{
    static char path [PATH_MAX];
    char buffer [PATH_MAX];
    
    S9xTextMode ();
    printf ("\n%s (default: %s): ", title, def);
    fflush (stdout);
    if (fgets (buffer, sizeof (buffer) - 1, stdin))
    {
	char *p = buffer;
	while (isspace (*p) || *p == '\n')
	    p++;
	if (!*p)
	{
	    strcpy (buffer, def);
	    p = buffer;
	}

	char *q = strrchr (p, '\n');
	if (q)
	    *q = 0;

	char fname [PATH_MAX];
	char drive [_MAX_DRIVE];
	char dir [_MAX_DIR];
	char ext [_MAX_EXT];

	_splitpath (p, drive, dir, fname, ext);
	_makepath (path, drive, *dir ? dir : dir1, fname, *ext ? ext : ext1);
	S9xGraphicsMode ();
	return (path);
    }
    S9xGraphicsMode ();
    return (NULL);
}

void S9xParseDisplayArg (char **argv, int &ind, int)
{
    if (strcasecmp (argv [ind], "-scale") == 0 ||
	strcasecmp (argv [ind], "-sc") == 0)
	stretch = TRUE;
    else
    if (strcasecmp (argv [ind], "-y") == 0 ||
	strcasecmp (argv [ind], "-interpolation") == 0)
    {
	interpolation = TRUE;
	Settings.SixteenBit = TRUE;
	Settings.SupportHiRes = TRUE;
	Settings.Transparency = TRUE;
    }
    else
	S9xUsage ();
}

void S9xExtraUsage ()
{
}

bool8 S9xReadMousePosition (int /* which1 */, int &/* x */, int & /* y */,
			    uint32 & /* buttons */)
{
    return (FALSE);
}

bool8 S9xReadSuperScopePosition (int & /* x */, int & /* y */, 
				 uint32 & /* buttons */)
{
    return (FALSE);
}
#endif

void S9xMessage (int /* type */, int /* number */, const char *message)
{
    fprintf (stderr, "%s\n", message);
}
#endif
