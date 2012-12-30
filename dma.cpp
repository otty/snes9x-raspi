/*******************************************************************************
  Snes9x - Portable Super Nintendo Entertainment System (TM) emulator.
 
  (c) Copyright 1996 - 2003 Gary Henderson (gary.henderson@ntlworld.com) and
                            Jerremy Koot (jkoot@snes9x.com)

  (c) Copyright 2002 - 2003 Matthew Kendora and
                            Brad Jorsch (anomie@users.sourceforge.net)
 

                      
  C4 x86 assembler and some C emulation code
  (c) Copyright 2000 - 2003 zsKnight (zsknight@zsnes.com),
                            _Demo_ (_demo_@zsnes.com), and
                            Nach (n-a-c-h@users.sourceforge.net)
                                          
  C4 C++ code
  (c) Copyright 2003 Brad Jorsch

  DSP-1 emulator code
  (c) Copyright 1998 - 2003 Ivar (ivar@snes9x.com), _Demo_, Gary Henderson,
                            John Weidman (jweidman@slip.net),
                            neviksti (neviksti@hotmail.com), and
                            Kris Bleakley (stinkfish@bigpond.com)
 
  DSP-2 emulator code
  (c) Copyright 2003 Kris Bleakley, John Weidman, neviksti, Matthew Kendora, and
                     Lord Nightmare (lord_nightmare@users.sourceforge.net

  OBC1 emulator code
  (c) Copyright 2001 - 2003 zsKnight, pagefault (pagefault@zsnes.com)
  Ported from x86 assembler to C by sanmaiwashi

  SPC7110 and RTC C++ emulator code
  (c) Copyright 2002 Matthew Kendora with research by
                     zsKnight, John Weidman, and Dark Force

  S-RTC C emulator code
  (c) Copyright 2001 John Weidman
  
  Super FX x86 assembler emulator code 
  (c) Copyright 1998 - 2003 zsKnight, _Demo_, and pagefault 

  Super FX C emulator code 
  (c) Copyright 1997 - 1999 Ivar and Gary Henderson.



 
  Specific ports contains the works of other authors. See headers in
  individual files.
 
  Snes9x homepage: http://www.snes9x.com
 
  Permission to use, copy, modify and distribute Snes9x in both binary and
  source form, for non-commercial purposes, is hereby granted without fee,
  providing that this license information and copyright notice appear with
  all copies and any derived work.
 
  This software is provided 'as-is', without any express or implied
  warranty. In no event shall the authors be held liable for any damages
  arising from the use of this software.
 
  Snes9x is freeware for PERSONAL USE only. Commercial users should
  seek permission of the copyright holders first. Commercial use includes
  charging money for Snes9x or software derived from Snes9x.
 
  The copyright holders request that bug fixes and improvements to the code
  should be forwarded to them so everyone can benefit from the modifications
  in future versions.
 
  Super NES and Super Nintendo Entertainment System are trademarks of
  Nintendo Co., Limited and its subsidiary companies.
*******************************************************************************/


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "snes9x.h"

#include "memmap.h"
#include "ppu.h"
#include "cpuexec.h"
#include "missing.h"
#include "dma.h"
#include "apu.h"
#include "gfx.h"
#include "sa1.h"
#include "spc7110.h"

extern int HDMA_ModeByteCounts [8];
extern uint8 *HDMAMemPointers [8];
extern uint8 *HDMABasePointers [8];
#ifndef _ZAURUS
#if defined(__linux__) || defined(__WIN32__)
static int S9xCompareSDD1IndexEntries (const void *p1, const void *p2)
{
    return (*(uint32 *) p1 - *(uint32 *) p2);
}
#endif
#endif
/**********************************************************************************************/
/* S9xDoDMA()                                                                                   */
/* This function preforms the general dma transfer                                            */
/**********************************************************************************************/

void S9xDoDMA (uint8 Channel)
{
    uint8 Work;
	struct SCPUState *cpu = &CPU;
	struct SIAPU *iapu = &IAPU;
	struct SAPU *apu = &APU;
	struct InternalPPU *ippu = &IPPU;
	struct SPPU *ppu = &PPU;

    if (Channel > 7 || cpu->InDMA)
		return;
	
    cpu->InDMA = TRUE;
    bool8_32 in_sa1_dma = FALSE;
#ifndef _ZAURUS
    uint8 *in_sdd1_dma = NULL;
	uint8 *spc7110_dma=NULL;
	bool8_32 s7_wrap=false;
#endif
    SDMA *d = &DMA[Channel];
	

    int count = d->TransferBytes;
	
    if (count == 0)
		count = 0x10000;
	
    int inc = d->AAddressFixed ? 0 : (!d->AAddressDecrement ? 1 : -1);

	if((d->ABank==0x7E||d->ABank==0x7F)&&d->BAddress==0x80)
	{
		d->AAddress+= d->TransferBytes;
		goto update_address;
	}
    switch (d->BAddress)
    {
    case 0x18:
    case 0x19:
		if (ippu->RenderThisFrame)
			FLUSH_REDRAW ();
		break;
    }
#ifndef _ZAURUS
    if (Settings.SDD1)
    {
		if (d->AAddressFixed && Memory.FillRAM [0x4801] > 0)
		{
			// Hacky support for pre-decompressed S-DD1 data
			inc = !d->AAddressDecrement ? 1 : -1;
			uint32 address = (((d->ABank << 16) | d->AAddress) & 0xfffff) << 4;
			
			address |= Memory.FillRAM [0x4804 + ((d->ABank - 0xc0) >> 4)];
#if defined(__linux__) || defined (__WIN32__)
			void *ptr = bsearch (&address, Memory.SDD1Index, 
				Memory.SDD1Entries, 12, S9xCompareSDD1IndexEntries);
			if (ptr)
				in_sdd1_dma = *(uint32 *) ((uint8 *) ptr + 4) + Memory.SDD1Data;
#else
			uint8 *ptr = Memory.SDD1Index;
			
			for (uint32 e = 0; e < Memory.SDD1Entries; e++, ptr += 12)
			{
				if (address == *(uint32 *) ptr)
				{
					in_sdd1_dma = *(uint32 *) (ptr + 4) + Memory.SDD1Data;
					break;
				}
			}
#endif
			
			if (!in_sdd1_dma)
			{
				// No matching decompressed data found. Must be some new 
				// graphics not encountered before. Log it if it hasn't been
				// already.
				uint8 *p = Memory.SDD1LoggedData;
				bool8_32 found = FALSE;
				uint8 SDD1Bank = Memory.FillRAM [0x4804 + ((d->ABank - 0xc0) >> 4)] | 0xf0;
				
				for (uint32 i = 0; i < Memory.SDD1LoggedDataCount; i++, p += 8)
				{
					if (*p == d->ABank ||
						*(p + 1) == (d->AAddress >> 8) &&
						*(p + 2) == (d->AAddress & 0xff) &&
						*(p + 3) == (count >> 8) &&
						*(p + 4) == (count & 0xff) &&
						*(p + 7) == SDD1Bank)
					{
						found = TRUE;
						break;
					}
				}
				if (!found && Memory.SDD1LoggedDataCount < MEMMAP_MAX_SDD1_LOGGED_ENTRIES)
				{
					*p = d->ABank;
					*(p + 1) = d->AAddress >> 8;
					*(p + 2) = d->AAddress & 0xff;
					*(p + 3) = count >> 8;
					*(p + 4) = count & 0xff;
					*(p + 7) = SDD1Bank;
					Memory.SDD1LoggedDataCount += 1;
				}
			}
		}
		Memory.FillRAM [0x4801] = 0;
    }
	if(Settings.SPC7110&&(d->AAddress==0x4800||d->ABank==0x50))
	{
		uint32 i,j;
		i=(s7r.reg4805|(s7r.reg4806<<8));
#ifdef SPC7110_DEBUG
		printf("DMA Transfer of %04X bytes from %02X%02X%02X:%02X, offset of %04X, internal bank of %04X, multiplier %02X\n",d->TransferBytes,s7r.reg4803,s7r.reg4802,s7r.reg4801, s7r.reg4804,i,  s7r.bank50Internal, s7r.AlignBy);
#endif
		i*=s7r.AlignBy;
		i+=s7r.bank50Internal;
		i%=DECOMP_BUFFER_SIZE;
		j=0;
		if((i+d->TransferBytes)<DECOMP_BUFFER_SIZE)
		{
			spc7110_dma=&s7r.bank50[i];
		}
		else
		{
			spc7110_dma=new uint8[d->TransferBytes];
			j=DECOMP_BUFFER_SIZE-i;
			memcpy(spc7110_dma, &s7r.bank50[i], j);
			memcpy(&spc7110_dma[j],s7r.bank50,d->TransferBytes-j);
			s7_wrap=true;
		}
		int icount=s7r.reg4809|(s7r.reg480A<<8);
		icount-=d->TransferBytes;
		s7r.reg4809=0x00ff&icount;
		s7r.reg480A=(0xff00&icount)>>8;

		s7r.bank50Internal+=d->TransferBytes;
		s7r.bank50Internal%=DECOMP_BUFFER_SIZE;
		inc=1;
		d->AAddress-=count;
	}
    if (d->BAddress == 0x18 && SA1.in_char_dma && (d->ABank & 0xf0) == 0x40)
    {
		// Perform packed bitmap to PPU character format conversion on the
		// data before transmitting it to V-RAM via-DMA.
		int num_chars = 1 << ((Memory.FillRAM [0x2231] >> 2) & 7);
		int depth = (Memory.FillRAM [0x2231] & 3) == 0 ? 8 :
		(Memory.FillRAM [0x2231] & 3) == 1 ? 4 : 2;
		
		int bytes_per_char = 8 * depth;
		int bytes_per_line = depth * num_chars;
		int char_line_bytes = bytes_per_char * num_chars;
		uint32 addr = (d->AAddress / char_line_bytes) * char_line_bytes;
		uint8 *base = GetBasePointer ((d->ABank << 16) + addr) + addr;
		uint8 *buffer = &Memory.ROM [CMemory::MAX_ROM_SIZE - 0x10000];
		uint8 *p = buffer;
		uint32 inc = char_line_bytes - (d->AAddress % char_line_bytes);
		uint32 char_count = inc / bytes_per_char;
		
		in_sa1_dma = TRUE;
		
		//printf ("%08x,", base); fflush (stdout);
		//printf ("depth = %d, count = %d, bytes_per_char = %d, bytes_per_line = %d, num_chars = %d, char_line_bytes = %d\n",
		//depth, count, bytes_per_char, bytes_per_line, num_chars, char_line_bytes);
		int i;
		
		switch (depth)
		{
		case 2:
			for (i = 0; i < count; i += inc, base += char_line_bytes, 
				inc = char_line_bytes, char_count = num_chars)
			{
				uint8 *line = base + (num_chars - char_count) * 2;
				for (uint32 j = 0; j < char_count && p - buffer < count; 
				j++, line += 2)
				{
					uint8 *q = line;
					for (int l = 0; l < 8; l++, q += bytes_per_line)
					{
						for (int b = 0; b < 2; b++)
						{
							uint8 r = *(q + b);
							*(p + 0) = (*(p + 0) << 1) | ((r >> 0) & 1);
							*(p + 1) = (*(p + 1) << 1) | ((r >> 1) & 1);
							*(p + 0) = (*(p + 0) << 1) | ((r >> 2) & 1);
							*(p + 1) = (*(p + 1) << 1) | ((r >> 3) & 1);
							*(p + 0) = (*(p + 0) << 1) | ((r >> 4) & 1);
							*(p + 1) = (*(p + 1) << 1) | ((r >> 5) & 1);
							*(p + 0) = (*(p + 0) << 1) | ((r >> 6) & 1);
							*(p + 1) = (*(p + 1) << 1) | ((r >> 7) & 1);
						}
						p += 2;
					}
				}
			}
			break;
		case 4:
			for (i = 0; i < count; i += inc, base += char_line_bytes, 
				inc = char_line_bytes, char_count = num_chars)
			{
				uint8 *line = base + (num_chars - char_count) * 4;
				for (uint32 j = 0; j < char_count && p - buffer < count; 
				j++, line += 4)
				{
					uint8 *q = line;
					for (int l = 0; l < 8; l++, q += bytes_per_line)
					{
						for (int b = 0; b < 4; b++)
						{
							uint8 r = *(q + b);
							*(p +  0) = (*(p +  0) << 1) | ((r >> 0) & 1);
							*(p +  1) = (*(p +  1) << 1) | ((r >> 1) & 1);
							*(p + 16) = (*(p + 16) << 1) | ((r >> 2) & 1);
							*(p + 17) = (*(p + 17) << 1) | ((r >> 3) & 1);
							*(p +  0) = (*(p +  0) << 1) | ((r >> 4) & 1);
							*(p +  1) = (*(p +  1) << 1) | ((r >> 5) & 1);
							*(p + 16) = (*(p + 16) << 1) | ((r >> 6) & 1);
							*(p + 17) = (*(p + 17) << 1) | ((r >> 7) & 1);
						}
						p += 2;
					}
					p += 32 - 16;
				}
			}
			break;
		case 8:
			for (i = 0; i < count; i += inc, base += char_line_bytes, 
				inc = char_line_bytes, char_count = num_chars)
			{
				uint8 *line = base + (num_chars - char_count) * 8;
				for (uint32 j = 0; j < char_count && p - buffer < count; 
				j++, line += 8)
				{
					uint8 *q = line;
					for (int l = 0; l < 8; l++, q += bytes_per_line)
					{
						for (int b = 0; b < 8; b++)
						{
							uint8 r = *(q + b);
							*(p +  0) = (*(p +  0) << 1) | ((r >> 0) & 1);
							*(p +  1) = (*(p +  1) << 1) | ((r >> 1) & 1);
							*(p + 16) = (*(p + 16) << 1) | ((r >> 2) & 1);
							*(p + 17) = (*(p + 17) << 1) | ((r >> 3) & 1);
							*(p + 32) = (*(p + 32) << 1) | ((r >> 4) & 1);
							*(p + 33) = (*(p + 33) << 1) | ((r >> 5) & 1);
							*(p + 48) = (*(p + 48) << 1) | ((r >> 6) & 1);
							*(p + 49) = (*(p + 49) << 1) | ((r >> 7) & 1);
						}
						p += 2;
					}
					p += 64 - 16;
				}
			}
			break;
		}
    }
#endif
#ifdef DEBUGGER
    if (Settings.TraceDMA)
    {
		sprintf (String, "DMA[%d]: %s Mode: %d 0x%02X%04X->0x21%02X Bytes: %d (%s) V-Line:%ld",
			Channel, d->TransferDirection ? "read" : "write",
			d->TransferMode, d->ABank, d->AAddress,
			d->BAddress, d->TransferBytes,
			d->AAddressFixed ? "fixed" :
		(d->AAddressDecrement ? "dec" : "inc"),
			cpu->V_Counter);
		if (d->BAddress == 0x18 || d->BAddress == 0x19)
			sprintf (String, "%s VRAM: %04X (%d,%d) %s", String,
				ppu->VMA.Address,
				ppu->VMA.Increment, ppu->VMA.FullGraphicCount,
				ppu->VMA.High ? "word" : "byte");

		else
			if (d->BAddress == 0x22)
			
				sprintf (String, "%s CGRAM: %02X (%x)", String, ppu->CGADD,
					ppu->CGFLIP);			
			else
				if (d->BAddress == 0x04)
					sprintf (String, "%s OBJADDR: %04X", String, ppu->OAMAddr);
				S9xMessage (S9X_TRACE, S9X_DMA_TRACE, String);
    }
#endif
	
    if (!d->TransferDirection)
    {
#ifdef VAR_CYCLES
		//reflects extra cycle used by DMA
		cpu->Cycles += 8 * (count+1);
#else
		//needs fixing for the extra DMA cycle
		cpu->Cycles += (1+count) + ((1+count) >> 2);
#endif
		uint8 *base = GetBasePointer ((d->ABank << 16) + d->AAddress);
		uint16 p = d->AAddress;
		
		if (!base)
			base = Memory.ROM;
		
#ifndef _ZAURUS
		if (in_sa1_dma)
		{
			base = &Memory.ROM [CMemory::MAX_ROM_SIZE - 0x10000];
			p = 0;
		}
		if (in_sdd1_dma)
		{
			base = in_sdd1_dma;
			p = 0;
		}
		if(spc7110_dma)
		{
			base=spc7110_dma;
			p = 0;
		}
#endif
		if (inc > 0)
			d->AAddress += count;
		else
			if (inc < 0)
				d->AAddress -= count;
			
			if (d->TransferMode == 0 || d->TransferMode == 2)
			{
				switch (d->BAddress)
				{
				case 0x04:
					do
					{
						Work = *(base + p);
						REGISTER_2104(Work, &Memory, ippu, ppu);
						p += inc;
						CHECK_SOUND();
					} while (--count > 0);
					break;
				case 0x18:
					ippu->FirstVRAMRead = TRUE;
					if (!ppu->VMA.FullGraphicCount)
					{
						do
						{
							Work = *(base + p);
							REGISTER_2118_linear(Work, &Memory, ippu, ppu);
							p += inc;
							CHECK_SOUND();
						} while (--count > 0);
					}
					else
					{
						do
						{
							Work = *(base + p);
							REGISTER_2118_tile(Work, &Memory, ippu, ppu);
							p += inc;
							CHECK_SOUND();
						} while (--count > 0);
					}
					break;
				case 0x19:
					ippu->FirstVRAMRead = TRUE;
					if (!ppu->VMA.FullGraphicCount)
					{
						do
						{
							Work = *(base + p);
							REGISTER_2119_linear(Work, &Memory, ippu, ppu);
							p += inc;
							CHECK_SOUND();
						} while (--count > 0);
					}
					else
					{
						do
						{
							Work = *(base + p);
							REGISTER_2119_tile(Work, &Memory, ippu, ppu);
							p += inc;
							CHECK_SOUND();
						} while (--count > 0);
					}
					break;
				case 0x22:
					do
					{
						Work = *(base + p);
						REGISTER_2122(Work, &Memory, ippu, ppu);
						p += inc;
						CHECK_SOUND();
					} while (--count > 0);
					break;
				case 0x80:
					do
					{
						Work = *(base + p);
						REGISTER_2180(Work, &Memory, ippu, ppu);
						p += inc;
						CHECK_SOUND();
					} while (--count > 0);
					break;
				default:
					do
					{
						Work = *(base + p);
						S9xSetPPU (Work, 0x2100 + d->BAddress, ppu, ippu);
						p += inc;
						CHECK_SOUND();
					} while (--count > 0);
					break;
				}
			}
			else
				if (d->TransferMode == 1 || d->TransferMode == 5)
				{
					if (d->BAddress == 0x18)
					{
						// Write to V-RAM
						ippu->FirstVRAMRead = TRUE;
						if (!ppu->VMA.FullGraphicCount)
						{
							while (count > 1)
							{
								Work = *(base + p);
								REGISTER_2118_linear(Work, &Memory, ippu, ppu);
								p += inc;
								
								Work = *(base + p);
								REGISTER_2119_linear(Work, &Memory, ippu, ppu);
								p += inc;
								CHECK_SOUND();
								count -= 2;
							}
							if (count == 1)
							{
								Work = *(base + p);
								REGISTER_2118_linear(Work, &Memory, ippu, ppu);
								p += inc;
							}
						}
						else
						{
							while (count > 1)
							{
								Work = *(base + p);
								REGISTER_2118_tile(Work, &Memory, ippu, ppu);
								p += inc;
								
								Work = *(base + p);
								REGISTER_2119_tile(Work, &Memory, ippu, ppu);
								p += inc;
								CHECK_SOUND();
								count -= 2;
							}
							if (count == 1)
							{
								Work = *(base + p);
								REGISTER_2118_tile(Work, &Memory, ippu, ppu);
								p += inc;
							}
						}
					}
					else
					{
						// DMA mode 1 general case
						while (count > 1)
						{
							Work = *(base + p);
							S9xSetPPU (Work, 0x2100 + d->BAddress, ppu, ippu);
							p += inc;
							
							Work = *(base + p);
							S9xSetPPU (Work, 0x2101 + d->BAddress, ppu, ippu);
							p += inc;
							CHECK_SOUND();
							count -= 2;
						}
						if (count == 1)
						{
							Work = *(base + p);
							S9xSetPPU (Work, 0x2100 + d->BAddress, ppu, ippu);
							p += inc;
						}
					}
				}
				else
					if (d->TransferMode == 3)
					{
						do
						{
							Work = *(base + p);
							S9xSetPPU (Work, 0x2100 + d->BAddress, ppu, ippu);
							p += inc;
							if (count <= 1)
								break;
							
							Work = *(base + p);
							S9xSetPPU (Work, 0x2100 + d->BAddress, ppu, ippu);
							p += inc;
							if (count <= 2)
								break;
							
							Work = *(base + p);
							S9xSetPPU (Work, 0x2101 + d->BAddress, ppu, ippu);
							p += inc;
							if (count <= 3)
								break;
							
							Work = *(base + p);
							S9xSetPPU (Work, 0x2101 + d->BAddress, ppu, ippu);
							p += inc;
							CHECK_SOUND();
							count -= 4;
						} while (count > 0);
					}
					else
						if (d->TransferMode == 4)
						{
							do
							{
								Work = *(base + p);
								S9xSetPPU (Work, 0x2100 + d->BAddress, ppu, ippu);
								p += inc;
								if (count <= 1)
									break;
								
								Work = *(base + p);
								S9xSetPPU (Work, 0x2101 + d->BAddress, ppu, ippu);
								p += inc;
								if (count <= 2)
									break;
								
								Work = *(base + p);
								S9xSetPPU (Work, 0x2102 + d->BAddress, ppu, ippu);
								p += inc;
								if (count <= 3)
									break;
								
								Work = *(base + p);
								S9xSetPPU (Work, 0x2103 + d->BAddress, ppu, ippu);
								p += inc;
								CHECK_SOUND();
								count -= 4;
							} while (count > 0);
						}
						else
						{
#ifdef DEBUGGER
							//	    if (Settings.TraceDMA)
							{
								sprintf (String, "Unknown DMA transfer mode: %d on channel %d\n",
									d->TransferMode, Channel);
								S9xMessage (S9X_TRACE, S9X_DMA_TRACE, String);
							}
#endif
						}
    }
    else
    {
		do
		{
			switch (d->TransferMode)
			{
			case 0:
			case 2:
#ifndef VAR_CYCLES
				cpu->Cycles += 1;
#endif
				Work = S9xGetPPU (0x2100 + d->BAddress, ppu, &Memory);
				S9xSetByte (Work, (d->ABank << 16) + d->AAddress, cpu);
				d->AAddress += inc;
				--count;
				break;
				
			case 1:
			case 5:
#ifndef VAR_CYCLES
				cpu->Cycles += 3;
#endif
				Work = S9xGetPPU (0x2100 + d->BAddress, ppu, &Memory);
				S9xSetByte (Work, (d->ABank << 16) + d->AAddress, cpu);
				d->AAddress += inc;
				if (!--count)
					break;
				
				Work = S9xGetPPU (0x2101 + d->BAddress, ppu, &Memory);
				S9xSetByte (Work, (d->ABank << 16) + d->AAddress, cpu);
				d->AAddress += inc;
				count--;
				break;
				
			case 3:
#ifndef VAR_CYCLES
				cpu->Cycles += 6;
#endif
				Work = S9xGetPPU (0x2100 + d->BAddress, ppu, &Memory);
				S9xSetByte (Work, (d->ABank << 16) + d->AAddress, cpu);
				d->AAddress += inc;
				if (!--count)
					break;
				
				Work = S9xGetPPU (0x2100 + d->BAddress, ppu, &Memory);
				S9xSetByte (Work, (d->ABank << 16) + d->AAddress, cpu);
				d->AAddress += inc;
				if (!--count)
					break;
				
				Work = S9xGetPPU (0x2101 + d->BAddress, ppu, &Memory);
				S9xSetByte (Work, (d->ABank << 16) + d->AAddress, cpu);
				d->AAddress += inc;
				if (!--count)
					break;
				
				Work = S9xGetPPU (0x2101 + d->BAddress, ppu, &Memory);
				S9xSetByte (Work, (d->ABank << 16) + d->AAddress, cpu);
				d->AAddress += inc;
				count--;
				break;
				
			case 4:
#ifndef VAR_CYCLES
				cpu->Cycles += 6;
#endif
				Work = S9xGetPPU (0x2100 + d->BAddress, ppu, &Memory);
				S9xSetByte (Work, (d->ABank << 16) + d->AAddress, cpu);
				d->AAddress += inc;
				if (!--count)
					break;
				
				Work = S9xGetPPU (0x2101 + d->BAddress, ppu, &Memory);
				S9xSetByte (Work, (d->ABank << 16) + d->AAddress, cpu);
				d->AAddress += inc;
				if (!--count)
					break;
				
				Work = S9xGetPPU (0x2102 + d->BAddress, ppu, &Memory);
				S9xSetByte (Work, (d->ABank << 16) + d->AAddress, cpu);
				d->AAddress += inc;
				if (!--count)
					break;
				
				Work = S9xGetPPU (0x2103 + d->BAddress, ppu, &Memory);
				S9xSetByte (Work, (d->ABank << 16) + d->AAddress, cpu);
				d->AAddress += inc;
				count--;
				break;
				
			default:
#ifdef DEBUGGER
				if (1) //Settings.TraceDMA)
				{
					sprintf (String, "Unknown DMA transfer mode: %d on channel %d\n",
						d->TransferMode, Channel);
					S9xMessage (S9X_TRACE, S9X_DMA_TRACE, String);
				}
#endif
				count = 0;
				break;
			}
			CHECK_SOUND();
		} while (count);
    }
    
#ifdef SPC700_C
    iapu->APUExecuting = Settings.APUEnabled;
    APU_EXECUTE ();
#endif
#ifndef _ZAURUS
    while (cpu->Cycles > cpu->NextEvent)
		S9xDoHBlankProcessing (cpu, apu, iapu);

	if(Settings.SPC7110&&spc7110_dma)
	{
		if(spc7110_dma&&s7_wrap)
			delete [] spc7110_dma;
	}
#endif
update_address:
    // Super Punch-Out requires that the A-BUS address be updated after the
    // DMA transfer.
    Memory.FillRAM[0x4302 + (Channel << 4)] = (uint8) d->AAddress;
    Memory.FillRAM[0x4303 + (Channel << 4)] = d->AAddress >> 8;
	
    // Secret of the Mana requires that the DMA bytes transfer count be set to
    // zero when DMA has completed.
    Memory.FillRAM [0x4305 + (Channel << 4)] = 0;
    Memory.FillRAM [0x4306 + (Channel << 4)] = 0;
	
    DMA[Channel].IndirectAddress = 0;
    d->TransferBytes = 0;
    
    cpu->InDMA = FALSE;


}

void S9xStartHDMA ()
{
	struct InternalPPU *ippu = &IPPU;

    if (Settings.DisableHDMA)
		ippu->HDMA = 0;
    else {
		ippu->HDMA = Memory.FillRAM [0x420c];
#ifdef DEBUGGER
		missing.hdma_this_frame = IPPU.HDMA;
#endif
	}
    ippu->HDMAStarted = TRUE;
	
    for (register uint32 i = 0; i < 8; i++)
    {
		if (ippu->HDMA & (1 << i))
		{
			DMA [i].LineCount = 0;
			DMA [i].FirstLine = TRUE;
			DMA [i].Address = DMA [i].AAddress;
		}
		HDMAMemPointers [i] = NULL;
    }
}

#ifdef DEBUGGER
void S9xTraceSoundDSP (const char *s, int i1 = 0, int i2 = 0, int i3 = 0,
					   int i4 = 0, int i5 = 0, int i6 = 0, int i7 = 0);
#endif


uint8 S9xDoHDMA (struct InternalPPU *ippu, struct SPPU *ppu, struct SCPUState *cpu)
{
	uint8 byte = ippu->HDMA;
	struct SDMA *p = &DMA [0];
    
    int d = 0;
	
    for (uint8 mask = 1; mask; mask <<= 1, p++, d++)
    {
		if (byte & mask)
		{
			if (!p->LineCount)
			{
				uint8 line = S9xGetByte ((p->ABank << 16) + p->Address, cpu);
				if (line == 0x80)
				{
					p->Repeat = TRUE;
					p->LineCount = 128;
				}
				else
				{
					p->Repeat = !(line & 0x80);
					p->LineCount = line & 0x7f;
				}
				
				// Disable H-DMA'ing into V-RAM (register 2118) for Hook
				if (!p->LineCount || p->BAddress == 0x18)
				{
					byte &= ~mask;
					p->IndirectAddress += HDMAMemPointers [d] - HDMABasePointers [d];
					Memory.FillRAM [0x4305 + (d << 4)] = (uint8) p->IndirectAddress;
					Memory.FillRAM [0x4306 + (d << 4)] = p->IndirectAddress >> 8;
					continue;
				}
				
				p->Address++;
				p->FirstLine = 1;
				if (p->HDMAIndirectAddressing)
				{
					p->IndirectBank = Memory.FillRAM [0x4307 + (d << 4)];
					p->IndirectAddress = S9xGetWord ((p->ABank << 16) + p->Address, cpu);
					p->Address += 2;
				}
				else
				{
					p->IndirectBank = p->ABank;
					p->IndirectAddress = p->Address;
				}
				HDMABasePointers [d] = HDMAMemPointers [d] = 
					S9xGetMemPointer ((p->IndirectBank << 16) + p->IndirectAddress);
			}
			else
			{

			if (!HDMAMemPointers [d])
			{
					if (!p->HDMAIndirectAddressing)
				{
					p->IndirectBank = p->ABank;
					p->IndirectAddress = p->Address;
				}

				if (!(HDMABasePointers [d] = HDMAMemPointers [d] = 
					S9xGetMemPointer ((p->IndirectBank << 16) + p->IndirectAddress)))
				{
					byte &= ~mask;
					continue;
				}
				// Uncommenting the following line breaks Punchout - it starts
				// H-DMA during the frame.
				//p->FirstLine = TRUE;
			}
			}
			if (p->Repeat && !p->FirstLine)
			{
				p->LineCount--;
				continue;
			}
			
#ifdef DEBUGGER
			if (Settings.TraceSoundDSP && p->FirstLine && 
				p->BAddress >= 0x40 && p->BAddress <= 0x43)
				S9xTraceSoundDSP ("Spooling data!!!\n");
			if (Settings.TraceHDMA && p->FirstLine)
			{
				sprintf (String, "H-DMA[%d] (%d) 0x%02X%04X->0x21%02X %s, Count: %3d, Rep: %s, V-LINE: %3ld %02X%04X",
					p-DMA, p->TransferMode, p->IndirectBank,
					p->IndirectAddress,
					p->BAddress,
					p->HDMAIndirectAddressing ? "ind" : "abs",
					p->LineCount,
					p->Repeat ? "yes" : "no ", cpu->V_Counter,
					p->ABank, p->Address);
				S9xMessage (S9X_TRACE, S9X_HDMA_TRACE, String);
			}
#endif

			switch (p->TransferMode)
			{
			case 0:
#ifndef VAR_CYCLES
				cpu->Cycles += 1;
#else
				cpu->Cycles += 8;
#endif
				S9xSetPPU (*HDMAMemPointers [d]++, 0x2100 + p->BAddress, ppu, ippu);
				break;
			case 1:
			case 5:
#ifndef VAR_CYCLES
				cpu->Cycles += 3;
#else
				cpu->Cycles += 16;
#endif
				S9xSetPPU (*(HDMAMemPointers [d] + 0), 0x2100 + p->BAddress, ppu, ippu);
				S9xSetPPU (*(HDMAMemPointers [d] + 1), 0x2101 + p->BAddress, ppu, ippu);
				HDMAMemPointers [d] += 2;
				break;
			case 2:
			case 6:
#ifndef VAR_CYCLES
				cpu->Cycles += 3;
#else
				cpu->Cycles += 16;
#endif
				S9xSetPPU (*(HDMAMemPointers [d] + 0), 0x2100 + p->BAddress, ppu, ippu);
				S9xSetPPU (*(HDMAMemPointers [d] + 1), 0x2100 + p->BAddress, ppu, ippu);
				HDMAMemPointers [d] += 2;
				break;
			case 3:
			case 7:
#ifndef VAR_CYCLES
				cpu->Cycles += 6;
#else
				cpu->Cycles += 32;
#endif
				S9xSetPPU (*(HDMAMemPointers [d] + 0), 0x2100 + p->BAddress, ppu, ippu);
				S9xSetPPU (*(HDMAMemPointers [d] + 1), 0x2100 + p->BAddress, ppu, ippu);
				S9xSetPPU (*(HDMAMemPointers [d] + 2), 0x2101 + p->BAddress, ppu, ippu);
				S9xSetPPU (*(HDMAMemPointers [d] + 3), 0x2101 + p->BAddress, ppu, ippu);
				HDMAMemPointers [d] += 4;
				break;
			case 4:
#ifndef VAR_CYCLES
				cpu->Cycles += 6;
#else
				cpu->Cycles += 32;
#endif
				S9xSetPPU (*(HDMAMemPointers [d] + 0), 0x2100 + p->BAddress, ppu, ippu);
				S9xSetPPU (*(HDMAMemPointers [d] + 1), 0x2101 + p->BAddress, ppu, ippu);
				S9xSetPPU (*(HDMAMemPointers [d] + 2), 0x2102 + p->BAddress, ppu, ippu);
				S9xSetPPU (*(HDMAMemPointers [d] + 3), 0x2103 + p->BAddress, ppu, ippu);
				HDMAMemPointers [d] += 4;
				break;
			}
			if (!p->HDMAIndirectAddressing)
				p->Address += HDMA_ModeByteCounts [p->TransferMode];
			p->FirstLine = FALSE;
			p->LineCount--;
	}
    }
    return (byte);
}

void S9xResetDMA ()
{
    int d;
    for (d = 0; d < 8; d++)
    {
		DMA [d].TransferDirection = FALSE;
		DMA [d].HDMAIndirectAddressing = FALSE;
		DMA [d].AAddressFixed = TRUE;
		DMA [d].AAddressDecrement = FALSE;
		DMA [d].TransferMode = 0xff;
		DMA [d].ABank = 0xff;
		DMA [d].AAddress = 0xffff;
		DMA [d].Address = 0xffff;
		DMA [d].BAddress = 0xff;
		DMA [d].TransferBytes = 0xffff;
    }
    for (int c = 0x4300; c < 0x4380; c += 0x10)
    {
		for (d = c; d < c + 12; d++)
			Memory.FillRAM [d] = 0xff;
		
		Memory.FillRAM [c + 0xf] = 0xff;
    }
}
