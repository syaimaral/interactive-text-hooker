﻿/*  Copyright (C) 2010-2011  kaosu (qiupf2000@gmail.com)
 *  This file is part of the Interactive Text Hooker.

 *  Interactive Text Hooker is free software: you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License as published
 *  by the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.

 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.

 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <windows.h>
#include "..\ithdll.h"
#include "..\sys.h"

WCHAR process_name[MAX_PATH];
HANDLE hEngineOn;
struct CodeSection
{
	DWORD base;
	DWORD size;
};
static WCHAR engine[0x20];
static DWORD module_base, module_limit;
static LPVOID trigger_addr;
static union {
	char text_buffer[0x1000];
	wchar_t wc_buffer[0x800];
	CodeSection code_section[0x200];
};
static char text_buffer_prev[0x1000];
static DWORD buffer_index,buffer_length;
extern BYTE LeadByteTable[0x100];
typedef bool (*tfun)(LPVOID addr, DWORD frame, DWORD stack);
static tfun trigger_fun;
void inline GetName()
{
	PLDR_DATA_TABLE_ENTRY it;
	__asm
	{
		mov eax,fs:[0x30]
		mov eax,[eax+0xC]
		mov eax,[eax+0xC]
		mov it,eax
	}
	wcscpy(process_name,it->BaseDllName.Buffer);
}
BOOL WINAPI DllMain(HANDLE hModule, DWORD reason, LPVOID lpReserved)
{
	switch(reason)
	{
	case DLL_PROCESS_ATTACH:
		{
		LdrDisableThreadCalloutsForDll(hModule);	
		IthInitSystemService();
		GetName();
		swprintf(engine,L"ITH_ENGINE_%d",current_process_id);
		hEngineOn=IthCreateEvent(engine);
		NtSetEvent(hEngineOn,0);
		}
		break;
	case DLL_PROCESS_DETACH:	
		NtClearEvent(hEngineOn);
		NtClose(hEngineOn);
		IthCloseSystemService();
		break;
	}
	return TRUE;
}
DWORD GetCodeRange(DWORD hModule,DWORD *low, DWORD *high)
{
	IMAGE_DOS_HEADER *DosHdr;
	IMAGE_NT_HEADERS *NtHdr;
	DWORD dwReadAddr;
	IMAGE_SECTION_HEADER *shdr;
	DosHdr=(IMAGE_DOS_HEADER*)hModule;
	if (IMAGE_DOS_SIGNATURE==DosHdr->e_magic)
	{
		dwReadAddr=hModule+DosHdr->e_lfanew;
		NtHdr=(IMAGE_NT_HEADERS*)dwReadAddr;
		if (IMAGE_NT_SIGNATURE==NtHdr->Signature)
		{
			shdr=(PIMAGE_SECTION_HEADER)((DWORD)(&NtHdr->OptionalHeader)+NtHdr->FileHeader.SizeOfOptionalHeader);
			while ((shdr->Characteristics&IMAGE_SCN_CNT_CODE)==0) shdr++;
			*low=hModule+shdr->VirtualAddress;
				*high=*low+(shdr->Misc.VirtualSize&0xFFFFF000)+0x1000;
		}
	}
	return 0;
}
inline DWORD SigMask(DWORD sig)
{
	__asm
	{
		xor ecx,ecx
		mov eax,sig
_mask:
		shr eax,8
		inc ecx
		test eax,eax
		jnz _mask
		sub ecx,4
		neg ecx
		or eax,-1
		shl ecx,3
		shr eax,cl
	}
}
DWORD FindCallAndEntryBoth(DWORD fun, DWORD size, DWORD pt, DWORD sig)
{
	//WCHAR str[0x40];
	DWORD i,j,t,l,mask;
	DWORD reverse_length=0x800;
	mask=SigMask(sig);
	bool flag1,flag2;
	for (i=0x1000;i<size-4;i++)
	{
		flag1=false;
		if (*(BYTE*)(pt+i)==0xE8)
		{
			flag1=true;flag2=true;
			t=*(DWORD*)(pt+i+1);
		}
		else if (*(WORD*)(pt+i)==0x15FF)
		{
			flag1=true;flag2=false;
			t=*(DWORD*)(pt+i+2);
		}
		if (flag1)
		{
			if (flag2)
			{
				flag1=(pt+i+5+t==fun);
				l=5;
			}
			else if (t>=pt&&t<=pt+size-4)
			{
				flag1=(*(DWORD*)t==fun);
				l=6;
			}
			else flag1=false;
			if (flag1)
			{
				//swprintf(str,L"CALL addr: 0x%.8X",pt+i);
				//OutputConsole(str);
				for (j=i;j>i-reverse_length;j--)
					if ((*(WORD*)(pt+j))==(sig&mask)) //Fun entry 1.
					{
						//swprintf(str,L"Entry: 0x%.8X",pt+j);
						//OutputConsole(str);
						return pt+j;
					}
				}
			else i+=l;
		}
	}
	OutputConsole(L"Find call and entry failed.");
	return 0;
}
DWORD FindCallOrJmpRel(DWORD fun, DWORD size, DWORD pt, bool jmp)
{
	DWORD i,t;
	BYTE sig=(jmp)?0xE9:0xE8;
	for (i=0x1000;i<size-4;i++)
	{
		if (*(BYTE*)(pt+i)==sig)
		{
			t=*(DWORD*)(pt+i+1);
			if(pt+i+5+t==fun) 
			{
				//OutputDWORD(pt+i);
				return pt+i;
			}
			else i+=5;
		}
	}
	return 0;
}
DWORD FindCallOrJmpAbs(DWORD fun, DWORD size, DWORD pt, bool jmp)
{
	DWORD i,t;
	WORD sig=jmp?0x25FF:0x15FF;
	for (i=0x1000;i<size-4;i++)
	{
		if (*(WORD*)(pt+i)==sig)
		{
			t=*(DWORD*)(pt+i+2);
			if (t>pt&&t<pt+size)
				if(*(DWORD*)t==fun) return pt+i;
				else i+=5;
		}
	}
	return 0;
}
DWORD FindCallBoth(DWORD fun, DWORD size, DWORD pt)
{
	DWORD i,t;
	for (i=0x1000;i<size-4;i++)
	{
		if (*(BYTE*)(pt+i)==0xE8)
		{
			t=*(DWORD*)(pt+i+1)+pt+i+5;
			if (t==fun) return i;
		}
		if (*(WORD*)(pt+i)==0x15FF)
		{
			t=*(DWORD*)(pt+i+2);
			if (t>=pt&&t<=pt+size-4)
			{
				if (*(DWORD*)t==fun) return i;
				else i+=6;
			}
		}
	}
	return 0;
}
DWORD FindCallAndEntryAbs(DWORD fun, DWORD size, DWORD pt, DWORD sig)
{
	//WCHAR str[0x40];
	DWORD i,j,t,mask;
	DWORD reverse_length=0x800;
	mask=SigMask(sig);
	for (i=0x1000;i<size-4;i++)
	{
		if (*(WORD*)(pt+i)==0x15FF)
		{
			t=*(DWORD*)(pt+i+2);
			if (t>=pt&&t<=pt+size-4)
			{
				if (*(DWORD*)t==fun)
				{
				//swprintf(str,L"CALL addr: 0x%.8X",pt+i);
				//OutputConsole(str);
				for (j=i;j>i-reverse_length;j--)
					if ((*(DWORD*)(pt+j)&mask)==sig) //Fun entry 1.
					{
						//swprintf(str,L"Entry: 0x%.8X",pt+j);
						//OutputConsole(str);
						return pt+j;
					}
				}
			}
			else i+=6;
		}
	}
	OutputConsole(L"Find call and entry failed.");
	return 0;
}
DWORD FindCallAndEntryRel(DWORD fun, DWORD size, DWORD pt, DWORD sig)
{
	//WCHAR str[0x40];
	DWORD i,j,mask;
	DWORD reverse_length=0x800;
	mask=SigMask(sig);
	i=FindCallOrJmpRel(fun,size,pt,false);
	if (i)
		for (j=i;j>i-reverse_length;j--)
			if (((*(DWORD*)j)&mask)==sig) //Fun entry 1.
			{
				//swprintf(str,L"Entry: 0x%.8X",j);
				//OutputConsole(str);
				return j;
			}
			OutputConsole(L"Find call and entry failed.");
			return 0;
}
DWORD FindEntryAligned(DWORD start, DWORD back_range)
{
	DWORD i,j;
	start &= ~0xF;
	for (i = start, j = start - back_range; i > j; i-=0x10)
	{
		if (*(DWORD*)(i - 4)==0xCCCCCCCC ||
			*(DWORD*)(i - 4)==0xCCCCCCC3 ||
			(*(DWORD*)(i - 4)|0xFF)==0xCCCCC3FF ||
			*(WORD*)(i - 2)==0xCCC3 ||
			*(BYTE*)(i - 1)==0xC3
			)
			return i;
	}
	return 0;
}
/********************************************************************************************
KiriKiri hook:
	Usually there are xp3 files in the game folder but also exceptions.
	Find TVP(KIRIKIRI) in the version description is a much more precise way.

	KiriKiri1 correspond to AGTH KiriKiri hook, but this doesn't always work well.
	Find call to GetGlyphOutlineW and go to function header. EAX will point to a
	structure contains character (at 0x14, [EAX+0x14]) we want. To split names into 
	different threads AGTH uses [EAX], seems that this value stands for font size.
	Since KiriKiri is compiled by BCC and BCC fastcall uses EAX to pass the first 
	parameter. Here we choose EAX is reasonable. 
	KiriKiri2 is a redundant hook to catch text when 1 doesn't work. When this happens,
	usually there is a single GetTextExtentPoint32W contains irregular repetitions which
	is out of the scope of KS or KF. This time we find a point and split them into clean
	text threads. First find call to GetTextExtentPoint32W and step out of this function.
	Usually there is a small loop. It is this small loop messed up the text. We can find
	one ADD EBX,2 in this loop. It's clear that EBX is a string pointer goes through the 
	string. After the loop EBX will point to the end of the string. So EBX-2 is the last
	char and we insert hook here to extract it.
********************************************************************************************/
void SpecialHookKiriKiri(DWORD esp_base, const HookParam& hp, DWORD* data, DWORD* split, DWORD* len)
{
	DWORD p1,p2,p3;
	p1=*(DWORD*)(esp_base-0x14);
	p2=*(DWORD*)(esp_base-0x18);
	if ((p1>>16)==(p2>>16))
	{
		p3=*(DWORD*)p1;
		if (p3)
		{
			p3+=8;
			for (p2=p3+2;*(WORD*)p2;p2+=2);
			*len=p2-p3;
			*data=p3;
			p1=*(DWORD*)(esp_base-0x20);
			p1=*(DWORD*)(p1+0x74);
			*split=*(DWORD*)(esp_base+0x48)|p1;
		}
		else *len=0;
	}
	else *len=0;
}
void FindKiriKiriHook(DWORD fun, DWORD size, DWORD pt, DWORD flag)
{
	DWORD t,i,j,k,addr,sig;
	if (flag) sig=0x575653;
	else sig=0xEC8B55;
	//WCHAR str[0x40];
	t=0;
	for (i=0x1000;i<size-4;i++)
	{
		if (*(WORD*)(pt+i)==0x15FF)
		{
			addr=*(DWORD*)(pt+i+2);
			if (addr>=pt&&addr<=pt+size-4)
			if (*(DWORD*)addr==fun) t++;
			if (t==flag+1) //We find call to GetGlyphOutlineW or GetTextExtentPoint32W.
			{
				//swprintf(str,L"CALL addr:0x%.8X",i+pt);
				//OutputConsole(str);
				for (j=i;j>i-0x1000;j--)
				{					
					if (((*(DWORD*)(pt+j))&0xFFFFFF)==sig)
					{
						if (flag)  //We find the function entry. flag indicate 2 hooks.
						{
							t=0;  //KiriKiri2, we need to find call to this function.
							for (k=j+0x6000;k<j+0x8000;k++) //Empirical range.
								if (*(BYTE*)(pt+k)==0xE8)
								{
									if (k+5+*(DWORD*)(pt+k+1)==j) t++;
									if (t==2)
									{
										//for (k+=pt+0x14; *(WORD*)(k)!=0xC483;k++);
										//swprintf(str,L"Hook addr: 0x%.8X",pt+k);
										//OutputConsole(str);
										HookParam hp={};
										/*hp.addr=k;
										hp.extern_fun=(DWORD)SpecialHookKiriKiri;
										hp.type=NO_CONTEXT|EXTERN_HOOK|USING_UNICODE|USING_SPLIT;*/
										hp.addr=pt+k+0x14;
										hp.off=-0x14;
										hp.ind=-0x2;
										hp.split=-0xC;
										hp.length_offset=1;
										hp.type|=USING_UNICODE|NO_CONTEXT|USING_SPLIT|DATA_INDIRECT;
										NewHook(hp,L"KiriKiri2");
										return;
									}
								}
						}
						else
						{
							//swprintf(str,L"Hook addr: 0x%.8X",pt+j);
							//OutputConsole(str);
							HookParam hp={};
							hp.addr=(DWORD)pt+j;
							hp.off=-0x8;
							hp.ind=0x14;
							hp.split=-0x8;
							hp.length_offset=1;
							hp.type|=USING_UNICODE|DATA_INDIRECT|USING_SPLIT|SPLIT_INDIRECT;
							NewHook(hp,L"KiriKiri1");
						}
						return;
					}
				}
				OutputConsole(L"Failed to find function entry.");
			}
		}
	}
}
void InsertKiriKiriHook()
{
	FindKiriKiriHook((DWORD)GetGlyphOutlineW,module_limit-module_base,module_base,0);
	FindKiriKiriHook((DWORD)GetTextExtentPoint32W,module_limit-module_base,module_base,1);
	RegisterEngineType(ENGINE_KIRIKIRI);
}
/********************************************************************************************
BGI hook:
	Usually game folder contains BGI.*. After first run BGI.gdb appears.

	BGI engine has font caching issue so the strategy is simple.
	First find call to TextOutA or TextOutW then reverse to function entry point,
	until full text is caught.
	After 2 tries we will get to the right place. Use ESP value to split text since
	it's likely to be different for different calls.
********************************************************************************************/
void FindBGIHook(DWORD fun, DWORD size, DWORD pt, WORD sig)
{
	WCHAR str[0x40];
	DWORD i,j,k,l;
	i=fun;
	//i=FindCallBoth(fun,size,pt);
	if (i==0)
	{
		swprintf(str,L"Can't find BGI hook: %.8X.",fun);
		OutputConsole(str);
		return;
	}
	//swprintf(str,L"CALL addr: 0x%.8X",pt+i);
	//OutputConsole(str);
	for (j=i;j>i-0x100;j--)
		if ((*(WORD*)(pt+j))==sig) //Fun entry 1.
		{
			//swprintf(str,L"Entry 1: 0x%.8X",pt+j);
			//OutputConsole(str);
			for (k=i+0x100;k<i+0x800;k++)
				if (*(BYTE*)(pt+k)==0xE8) 
					if (k+5+*(DWORD*)(pt+k+1)==j) //Find call to fun1.
					{
						//swprintf(str,L"CALL to entry 1: 0x%.8X",pt+k);
						//OutputConsole(str);
						for (l=k;l>k-0x100;l--)
							if ((*(WORD*)(pt+l))==0xEC83) //Fun entry 2.
							{
								//swprintf(str,L"Entry 2(final): 0x%.8X",pt+l);
								//OutputConsole(str);
								HookParam hp={};
								hp.addr=(DWORD)pt+l;
								hp.off=0x8;
								hp.split=-0x18;
								hp.length_offset=1;
								hp.type|=BIG_ENDIAN|USING_SPLIT;
								NewHook(hp,L"BGI");
								return;
							}
					}
		}
}
bool InsertBGIDynamicHook(LPVOID addr, DWORD frame, DWORD stack)
{
	if (addr==TextOutA||addr==TextOutW)
	{
		DWORD i=*(DWORD*)(stack+4)-module_base;
		FindBGIHook(i,module_limit-module_base,module_base,0xEC83);
		RegisterEngineType(ENGINE_BGI);
		return true;
	}
	return false;
}
void InsertBGIHook()
{
	//FindBGIHook((DWORD)TextOutA,module_limit-module_base,module_base,0xEC83);
	//RegisterEngineType(ENGINE_BGI);
	OutputConsole(L"Probably BGI. Wait for text.");
	SwitchTrigger();
	trigger_fun=InsertBGIDynamicHook;
}
/********************************************************************************************
Reallive hook:
	Process name is reallive.exe or reallive*.exe.

	Technique to find Reallive hook is quite different from 2 above.
	Usually Reallive engine has a font caching issue. This time we wait
	until the first call to GetGlyphOutlineA. Reallive engine usually set
	up stack frames so we can just refer to EBP to find function entry.

********************************************************************************************/
bool InsertRealliveDynamicHook(LPVOID addr, DWORD frame, DWORD stack)
{
	if (addr!=GetGlyphOutlineA) return false;
	DWORD i,j;
	HookParam hp={};
	i=frame;
	if (i!=0)
	{
		i=*(DWORD*)(i+4);
		for (j=i;j>i-0x100;j--)
		if (*(WORD*)j==0xEC83)
		{
			hp.addr=j;
			hp.off=0x14;
			hp.split=-0x18;
			hp.length_offset=1;
			hp.type|=BIG_ENDIAN|USING_SPLIT;
			NewHook(hp,L"RealLive");
			RegisterEngineType(ENGINE_REALLIVE);
			return true;;
		}
	}
	return true;
}
void InsertRealliveHook()
{
	OutputConsole(L"Probably Reallive. Wait for text.");
	SwitchTrigger();
	trigger_fun=InsertRealliveDynamicHook;
}
void SpecialHookSiglus(DWORD esp_base, const HookParam& hp, DWORD* data, DWORD* split, DWORD* len)
{
	__asm
	{
		mov edx,esp_base
		mov ecx,[edx-0xC]
		mov eax,[ecx+0x14]
		add ecx,4
		cmp eax,0x8
		cmovnb ecx,[ecx]
		mov edx,len
		add eax,eax
		mov [edx],eax
		mov edx,data
		mov [edx],ecx
	}
}
void InsertSiglusHook()
{
	DWORD base=module_base;
	DWORD size=module_limit-module_base;
	size=size<0x200000?size:0x200000;
	BYTE ins[8]={0x33,0xC0,0x8B,0xF9,0x89,0x7C,0x24};
	int index=SearchPattern(module_base,size,ins,7);
	if (index==-1) 
	{
		OutputConsole(L"Unknown SiglusEngine");
		return;
	}
	base+=index;
	DWORD limit=base-0x100;
	while (base>limit)
	{
		if (*(WORD*)base==0xFF6A)
		{
			HookParam hp={};
			hp.addr=base;
			hp.extern_fun=(DWORD)SpecialHookSiglus;
			hp.type=EXTERN_HOOK|USING_UNICODE;
			NewHook(hp,L"SiglusEngine");
			RegisterEngineType(ENGINE_SIGLUS);
			return;
		}
		base--;
	}
}
/********************************************************************************************
MAJIRO hook:
	Game folder contains both data.arc and scenario.arc. arc files is
	quite common seen so we need 2 files to confirm it's MAJIRO engine.

	Font caching issue. Find call to TextOutA and the function entry.

	The original Majiro hook will catch furiga mixed with the text.
	To split them out we need to find a parameter. Seems there's no
	simple way to handle this case.
	At the function entry, EAX seems to point to a structure to describe 
	current	drawing context. +28 seems to be font size. +48 is negative
	if furigana. I don't know exact meaning of this structure,
	just do memory comparisons and get the value working for current release.

********************************************************************************************/
void SpecialHookMajiro(DWORD esp_base, const HookParam& hp, DWORD* data, DWORD* split, DWORD* len)
{
	__asm
	{
		mov edx,esp_base
		mov edi,[edx+0xC]
		mov eax,data
		mov [eax],edi
		or ecx,0xFFFFFFFF
		xor eax,eax
		repne scasb
		not ecx
		dec ecx
		mov eax,len
		mov [eax],ecx
		mov eax,[edx+4]
		mov edx,[eax+0x28]
		mov eax,[eax+0x48]
		sar eax,0x1F
		mov dh,al
		mov ecx,split
		mov [ecx],edx
	}
}
void InsertMajiroHook()
{
	HookParam hp={};

	/*hp.off=0xC;
	hp.split=4;
	hp.split_ind=0x28;
	hp.type|=USING_STRING|USING_SPLIT|SPLIT_INDIRECT;*/
	hp.addr=FindCallAndEntryAbs((DWORD)TextOutA,module_limit-module_base,module_base,0xEC81);
	hp.type=EXTERN_HOOK;
	hp.extern_fun=(DWORD)SpecialHookMajiro;
	NewHook(hp,L"MAJIRO");
	RegisterEngineType(ENGINE_MAJIRO);
}
/********************************************************************************************
CMVS hook:
	Process name is cmvs.exe or cnvs.exe or cmvs*.exe. Used by PurpleSoftware games.

	Font caching issue. Find call to GetGlyphOutlineA and the function entry.
********************************************************************************************/
void InsertCMVSHook()
{
	HookParam hp={};
	hp.off=0x8;
	hp.split=-0x18;
	hp.type|=BIG_ENDIAN|USING_SPLIT;
	hp.length_offset=1;
	hp.addr=FindCallAndEntryAbs((DWORD)GetGlyphOutlineA,module_limit-module_base,module_base,0xEC83);
	NewHook(hp,L"CMVS");
	RegisterEngineType(ENGINE_CMVS);
}
/********************************************************************************************
rUGP hook:
	Process name is rugp.exe. Used by AGE/GIGA games.

	Font caching issue. Find call to GetGlyphOutlineA and keep stepping out functions.
	After several tries we comes to address in rvmm.dll and everything is catched.
	We see CALL [E*X+0x*] while EBP contains the character data.
	It's not as simple to reverse in rugp at run time as like reallive since rugp dosen't set
	up stack frame. In other words EBP is used for other purpose. We need to find alternative
	approaches.
	The way to the entry of that function gives us clue to find it. There is one CMP EBP,0x8140 
	instruction in this function and that's enough! 0x8140 is the start of SHIFT-JIS 
	characters. It's determining if ebp contains a SHIFT-JIS character. This function is not likely
	to be used in other ways. We simply search for this instruction and place hook around.
********************************************************************************************/
void InsertRUGPHook()
{
	DWORD low,high,t;
	if (FillRange(L"rvmm.dll",&low,&high)==0) goto rt;
	WCHAR str[0x40];
	LPVOID ch=(LPVOID)0x8140;
	t=SearchPattern(low+0x20000,high-low-0x20000,&ch,4)+0x20000;
	BYTE* s=(BYTE*)(low+t);
	if (s[-2]!=0x81) goto rt;
	if (t!=-1)
	{
		
		for (int i=0;i<0x200;i++,s--)
			if (s[0]==0x90)
				if (*(DWORD*)(s-3)==0x90909090)
				{
					t=low+t-i+1;
					swprintf(str,L"HookAddr 0x%.8x",t);
					OutputConsole(str);
					HookParam hp={};
					hp.addr=t;
					hp.off=0x4;
					hp.length_offset=1;
					hp.type|=BIG_ENDIAN;
					NewHook(hp,L"rUGP");
					RegisterEngineType(ENGINE_RUGP);
					return;
				}
	}
	else
	{
		t=SearchPattern(low,0x20000,&s,4);
		if (t==-1) {OutputConsole(L"Can't find characteristic instruction.");return;}
		s=(BYTE*)(low+t);
		for (int i=0;i<0x200;i++,s--)
			if (s[0]==0x90)
				if (*(DWORD*)(s-3)==0x90909090)
				{
					t=low+t-i+1;
					swprintf(str,L"HookAddr 0x%.8x",t);
					OutputConsole(str);
					HookParam hp={};
					hp.addr=t;
					hp.off=0x4;
					hp.length_offset=1;
					hp.type|=BIG_ENDIAN;
					NewHook(hp,L"rUGP");
					RegisterEngineType(ENGINE_RUGP);
					return;
				}
	}
rt:
	OutputConsole(L"Unknown rUGP engine.");
}
/********************************************************************************************
Lucifen hook:
	Game folder contains *.lpk. Used by Navel games.
	Hook is same to GetTextExtentPoint32A, use ESP to split name.
********************************************************************************************/
void InsertLucifenHook()
{
	HookParam hp={};
	hp.addr=(DWORD)GetTextExtentPoint32A;
	hp.off=8;
	hp.split=-0x18;
	hp.length_offset=3;
	hp.type=USING_STRING|USING_SPLIT;
	NewHook(hp,L"Lucifen");
	RegisterEngineType(ENGINE_LUCIFEN);
}
/********************************************************************************************
System40 hook:
	System40 is a game engine developed by Alicesoft.
	Afaik, there are 2 very different types of System40. Each requires a particular hook.

	Pattern 1: Either SACTDX.dll or SACT2.dll exports SP_TextDraw.
	The first relative call in this function draw text to some surface.
	Text pointer is return by last absolute indirect call before that.
	Split parameter is a little tricky. The first register pushed onto stack at the begining
	usually is used as font size later. According to instruction opcode map, push
	eax -- 50, ecx -- 51, edx -- 52, ebx --53, esp -- 54, ebp -- 55, esi -- 56, edi -- 57
	Split parameter value:
	eax - -8,   ecx - -C,  edx - -10, ebx - -14, esp - -18, ebp - -1C, esi - -20, edi - -24
	Just extract the low 4 bit and shift left 2 bit, then minus by -8, 
	will give us the split parameter. e.g. push ebx 53->3 *4->C, -8-C=-14.
	Somtimes if split function is enabled, ITH will split text spoke by different
	character into different thread. Just open hook dialog and uncheck split parameter.
	Then click modify hook.

	Pattern 2: *engine.dll exports SP_SetTextSprite.
	At the entry point, EAX should be a pointer to some structre, character at +0x8.
	Before calling this function, the caller put EAX onto stack, we can also find this
	value on stack. But seems parameter order varies from game release. If a future
	game breaks the EAX rule then we need to disassemble the caller code to determine
	data offset dynamically.
********************************************************************************************/

void InsertAliceHook1(DWORD addr, DWORD module, DWORD limit)
{
	HookParam hp={};	
	DWORD c,i,j,s=addr;
	if (s==0) return;
	for (i=s;i<s+0x100;i++)
	{
		if (*(BYTE*)i==0xE8) //Find the first relative call.
		{
			j=i+5+*(DWORD*)(i+1);
			if (j>module&&j<limit)
			{
				while (1) //Find the first register push onto stack.
				{
					c=disasm((BYTE*)s);
					if (c==1) break;
					s+=c;
				}
				c=*(BYTE*)s; 
				hp.addr=j;
				hp.off=-0x8;
				hp.split=-8-((c&0xF)<<2);
				hp.type=USING_STRING|USING_SPLIT;
				//if (s>j) hp.type^=USING_SPLIT;
				NewHook(hp,L"System40");
				RegisterEngineType(ENGINE_SYS40);
				return;
			}
		}
	}
}
void InsertAliceHook2(DWORD addr)
{
	HookParam hp={};
	hp.addr=addr;
	if (hp.addr==0) return;
	hp.off=-0x8;
	hp.ind=0x8;
	hp.length_offset=1;
	hp.type=DATA_INDIRECT;
	NewHook(hp,L"System40");
	RegisterEngineType(ENGINE_SYS40);
}
/********************************************************************************************
AtelierKaguya hook:
	Game folder contains message.dat. Used by AtelierKaguya games.
	Usually has font caching issue with TextOutA.
	Game engine uses EBP to set up stack frame so we can easily trace back.
	Keep step out until it's in main game module. We notice that either register or
	stack contains string pointer before call instruction. But it's not quite stable. 
	In-depth analysis of the called function indicates that there's a loop tranverses
	the string one character by one. We can set a hook there.
	This search process is too complex so I just make use of some characteristic
	instruction(add esi,0x40) to locate the right point. 
********************************************************************************************/
void InsertAtelierHook()
{
	DWORD sig,i,j;
	//FillRange(process_name,&base,&size);
	//size=size-base;
	sig=0x40C683; //add esi,0x40
	i=module_base+SearchPattern(module_base,module_limit-module_base,&sig,3);
	for (j=i-0x200;i>j;i--)
	{
		if (*(DWORD*)i==0xFF6ACCCC) //Find the function entry
		{
			HookParam hp={};
			hp.addr=i+2;
			hp.off=8;
			hp.split=-0x18;
			hp.length_offset=1;
			hp.type=USING_SPLIT;
			NewHook(hp,L"Atelier KAGUYA");
			RegisterEngineType(ENGINE_ATELIER);
			return;
		}
	}
	OutputConsole(L"Unknown Atelier KAGUYA engine.");
}
/********************************************************************************************
CIRCUS hook:
	Game folder contains advdata folder. Used by CIRCUS games.
	Usually has font caching issues. But trace back from GetGlyphOutline gives a hook
	which generate repetition.
	If we study circus engine follow Freaka's video, we can easily discover that
	in the game main module there is a static buffer, which is filled by new text before
	it's drawing to screen. By setting a hardware breakpoint there we can locate the
	function filling the buffer. But we don't have to set hardware breakpoint to search
	the hook address if we know some characteristic instruction(cmp al,0x24) around there. 
********************************************************************************************/
void InsertCircusHook()
{
	DWORD i,j,k;
	//FillRange(process_name,&base,&size);
	for (i=module_base+0x1000;i<module_limit-4;i++)
		if (*(WORD*)i==0xA3C) //cmp al, 0xA; je
		{
			for (j=i;j<i+0x100;j++)
			{
				if (*(BYTE*)j==0xE8)
				{
					k=*(DWORD*)(j+1)+j+5;
					if (k>module_base&&k<module_limit)
					{
						HookParam hp={};
						hp.addr=k;
						hp.off=0xC;
						hp.split=-0x18;
						hp.length_offset=1;
						hp.type=DATA_INDIRECT|USING_SPLIT;
						NewHook(hp,L"CIRCUS");
						RegisterEngineType(ENGINE_CIRCUS);
						return;
					}
				}
			}
			break;
		}
	OutputConsole(L"Unknown CIRCUS engine");
}
void InsertCircusHook2()
{
	DWORD i,j;
	for (i=module_base+0x1000;i<module_limit-4;i++)
		if ((*(DWORD*)i&0xFFFFFF)==0x75243C) // cmp al, 24; je
		{
			j = FindEntryAligned(i,0x80);
			if (j)
			{
				HookParam hp={};
				hp.addr=j;
				hp.off=0x8;
				hp.type=USING_STRING;
				NewHook(hp,L"CIRCUS");
				RegisterEngineType(ENGINE_CIRCUS);
				return;
			}
			break;
		}
		OutputConsole(L"Unknown CIRCUS engine");
}
/********************************************************************************************
ShinaRio hook:
	Game folder contains rio.ini.
	Problem of default hook GetTextExtentPoint32A is that the text repeat one time.
	But KF just can't resolve the issue. ShinaRio engine always perform integrity check.
	So it's very difficult to insert a hook into the game module. Freaka suggests to refine
	the default hook by adding split parameter on the stack. So far there is 2 different
	version of ShinaRio engine that needs different split parameter. Seems this value is
	fixed to the last stack frame. We just navigate to the entry. There should be a
	sub esp,* instruction. This value plus 4 is just the offset we need.

	New ShinaRio engine (>=2.48) uses different approach.
********************************************************************************************/
void SpecialHookShina(DWORD esp_base, const HookParam& hp, DWORD* data, DWORD* split, DWORD* len)
{
	DWORD ptr=*(DWORD*)(esp_base-0x20);
	*split=ptr;
	char* str=*(char**)(ptr+0x160);
	strcpy(text_buffer,str);
	int skip=0;
	for (str=text_buffer;*str;str++)
	{
		if (str[0]==0x5F)
		{
			if (str[1]==0x72)
			{
				str[0]=str[1]=1;
			}
			else if (str[1]==0x74)
			{
				while (str[0]!=0x2F) *str++=1;
				*str=1;
			}
		}
	}
	for (str=text_buffer;str[skip];)
	{
		if (str[skip]==1) 
		{
			skip++;
		}
		else
		{
			str[0]=str[skip];
			str++;
		}
	}
	str[0]=0;
	if (strcmp(text_buffer,text_buffer_prev)==0) 
	{
		*len=0;
	}
	else
	{
		for (skip=0;text_buffer[skip];skip++)
			text_buffer_prev[skip]=text_buffer[skip];
		text_buffer_prev[skip]=0;
		*data=(DWORD)text_buffer_prev;
		*len=skip;
	}
}
void InsertShinaHook()
{
	HANDLE hFile=IthCreateFile(L"setup.ini",FILE_READ_DATA,FILE_SHARE_READ,FILE_OPEN);
	if (hFile!=INVALID_HANDLE_VALUE)
	{
		IO_STATUS_BLOCK ios;
		char *buffer,*version,*ptr;
		char small_buffer[0x40];
		WCHAR file[0x40];
		DWORD ver,i,j;
		buffer=text_buffer;
		NtReadFile(hFile,0,0,0,&ios,buffer,0x1000,0,0);
		NtClose(hFile);
		version=strstr(buffer,"椎名里緒");
		if (version)
		{
			version=strstr(buffer,"InstallFiles");
			for (ptr=version;*ptr!=0xD;ptr++);
			*ptr=0;
			_strlwr(version);
			ptr=strstr(version,"ini");
			for (version=ptr;*version!=',';version--);
			version++;
			while (*ptr!=',') ptr++;
			j=ptr-version;
			for (i=0;i<j;i++)
				file[i]=version[i];
			hFile=IthCreateFile(file,FILE_READ_DATA,FILE_SHARE_READ,FILE_OPEN);
			NtReadFile(hFile,0,0,0,&ios,small_buffer,0x40,0,0);
			NtClose(hFile);
			small_buffer[0x3F]=0;
			version=strstr(small_buffer,"v2.");
			ver=0;
			sscanf(version+0x3,"%d",&ver);
			if (ver>40)
			{
				HookParam hp={};
				if (ver>=48)
				{
					hp.addr=(DWORD)GetTextExtentPoint32A;
					hp.extern_fun=(DWORD)SpecialHookShina;
					hp.type=EXTERN_HOOK|USING_STRING;
					NewHook(hp,L"ShinaRio");
					RegisterEngineType(ENGINE_SHINA);
				}
				else if (ver<48)
				{
					DWORD s;
					s=FindCallAndEntryBoth((DWORD)GetTextExtentPoint32A,
						module_limit-module_base,(DWORD)module_base,0xEC81);
					if (s)
					{
						hp.addr=(DWORD)GetTextExtentPoint32A;
						hp.off=0x8;
						hp.split=*(DWORD*)(s+2)+4;
						hp.length_offset=1;
						hp.type=DATA_INDIRECT|USING_SPLIT;
						NewHook(hp,L"ShinaRio");
						RegisterEngineType(ENGINE_SHINA);
					}
				}
			}
		}
		else
		{
			OutputConsole(L"Unknown ShinaRio engine");
		}
	}
}
bool InsertWaffleDynamicHook(LPVOID addr, DWORD frame, DWORD stack)
{
	if (addr != GetTextExtentPoint32A) return false;
	DWORD retn,limit,str;
	WORD ch;
	NTSTATUS status;
	MEMORY_BASIC_INFORMATION info;
	str = *(DWORD*)(stack+0xC);
	ch = *(WORD*)str;
	if (ch<0x100) return false;
	limit = (stack | 0xFFFF) + 1;
	for (stack += 0x10; stack < limit; stack += 4)
	{
		str = *(DWORD*)stack;
		if ((str >> 16) == (stack >> 16)) continue; //No stack
		status = NtQueryVirtualMemory(NtCurrentProcess(),(PVOID)str,MemoryBasicInformation,&info,sizeof(info),0);
		if (!NT_SUCCESS(status) || info.Protect & PAGE_NOACCESS) continue; //Accessible
		if (*(WORD*)(str + 4) == ch) break;
	}
	if (stack < limit)
	{
		for (limit = stack + 0x80; stack < limit ; stack += 4)
		if (*(DWORD*)stack == -1)
		{
			retn = *(DWORD*)(stack + 4);
			if (retn > module_base && retn < module_limit)
			{
				HookParam hp = {};
				hp.addr = retn + *(DWORD*)(retn - 4);
				hp.length_offset = 1;
				hp.off = -0x20;
				hp.ind = 4;
				//hp.split = 0x1E8;
				hp.type = DATA_INDIRECT;
				NewHook(hp, L"WAFFLE");
				RegisterEngineType(ENGINE_WAFFLE);
				return true;
			}

		}

	}
	OutputConsole(L"Unknown waffle engine.");
	return true;

}
void InsertWaffleHook()
{
	DWORD i;
	for (i = module_base + 0x1000; i < module_limit - 4; i++)
	{
		if (*(DWORD*)i == 0xAC68)
		{
			HookParam hp = {};
			hp.addr=i;
			hp.length_offset=1;
			hp.off=-0x20;
			hp.ind=4;
			hp.split=0x1E8;
			hp.type=DATA_INDIRECT|USING_SPLIT;
			NewHook(hp,L"WAFFLE");
			RegisterEngineType(ENGINE_WAFFLE);
			return;
		}
	}
	OutputConsole(L"Probably Waffle. Wait for text.");
	SwitchTrigger();
	trigger_fun=InsertWaffleDynamicHook;
}
void InsertTinkerBellHook()
{
	DWORD s1,s2,i;
	DWORD ch=0x8141;
	HookParam hp={};
	hp.off=0xC;
	hp.length_offset=1;
	hp.type=DATA_INDIRECT;
	s1=SearchPattern(module_base,module_limit-module_base-4,&ch,4);
	if (s1)
	{
		for (i=s1;i>s1-0x400;i--)
		{
			if (*(WORD*)(module_base+i)==0xEC83)
			{
				hp.addr=module_base+i;
				NewHook(hp,L"C.System");
				break;
			}
		}
	}
	s2=s1+SearchPattern(module_base+s1+4,module_limit-s1-8,&ch,4);
	if (s2)
	{
		for (i=s2;i>s2-0x400;i--)
		{
			if (*(WORD*)(module_base+i)==0xEC83)
			{
				hp.addr=module_base+i;
				NewHook(hp,L"TinkerBell");
				break;
			}
		}
	}
	RegisterEngineType(ENGINE_TINKER);
}
void InsertLuneHook()
{
	HookParam hp={};
	DWORD c=FindCallOrJmpAbs((DWORD)ExtTextOutA,module_limit-module_base,(DWORD)module_base,true);
	if (c==0) return;
	hp.addr=FindCallAndEntryRel(c,module_limit-module_base,(DWORD)module_base,0xEC8B55);
	if (hp.addr==0) return;
	hp.off=4;
	hp.type=USING_STRING;
	NewHook(hp,L"MBL-Furigana");
	c=FindCallOrJmpAbs((DWORD)GetGlyphOutlineA,module_limit-module_base,(DWORD)module_base,true);
	if (c==0) return;
	hp.addr=FindCallAndEntryRel(c,module_limit-module_base,(DWORD)module_base,0xEC8B55);
	if (hp.addr==0) return;
	hp.split=-0x18;
	hp.length_offset=1;
	hp.type=BIG_ENDIAN|USING_SPLIT;
	NewHook(hp,L"MBL");
	RegisterEngineType(ENGINE_LUNE);
}
/********************************************************************************************
YU-RIS hook:
	Become common recently. I first encounter this game in Whirlpool games.
	Problem is name is repeated multiple times.
	Step out of function call to TextOuA, just before call to this function,
	there should be a piece of code to calcuate the length of the name.
	This length is 2 for single character name and text,
	For a usual name this value is greater than 2.
********************************************************************************************/
void InsertWhirlpoolHook()
{
	DWORD i,t;
	DWORD entry=FindCallAndEntryBoth((DWORD)TextOutA,module_limit-module_base,module_base,0xEC83);
	if (entry==0) return;
	entry=FindCallAndEntryRel(entry-4,module_limit-module_base,module_base,0xEC83);
	if (entry==0) return;
	entry=FindCallOrJmpRel(entry-4,module_limit-module_base-0x10000,module_base+0x10000,false);
	for (i=entry-4;i>entry-0x100;i--)
	{
		if (*(WORD*)i==0xC085)
		{
			t=*(WORD*)(i+2);
			if ((t&0xFF)==0x76) {t=4;break;}
			if ((t&0xFFFF)==0x860F) {t=8;break;}
		}
	}
	if (i==entry-0x100) return;
	HookParam hp={};
	hp.addr=i+t;
	hp.off=-0x24;
	hp.split=-0x8;
	hp.type=USING_STRING|USING_SPLIT;
	NewHook(hp,L"YU-RIS");
	RegisterEngineType(ENGINE_WHIRLPOOL);
}
void InsertCotophaHook()
{	
	HookParam hp={};
	hp.addr=FindCallAndEntryAbs((DWORD)GetTextMetricsA,module_limit-module_base,module_base,0xEC8B55);
	if (hp.addr==0) return;
	hp.off=4;
	hp.split=-0x1C;
	hp.type=USING_UNICODE|USING_SPLIT|USING_STRING;
	NewHook(hp,L"Cotopha");
	RegisterEngineType(ENGINE_COTOPHA);
}
void InsertCatSystem2Hook()
{
	HookParam hp={};
	/*DWORD search=0x95EB60F;
	DWORD j,i=SearchPattern(module_base,module_limit-module_base,&search,4);
	if (i==0) return;
	i+=module_base;
	for (j=i-0x100;i>j;i--)
		if (*(DWORD*)i==0xCCCCCCCC) break;
	if (i==j) return;
	hp.addr=i+4;
	hp.off=-0x8;
	hp.ind=4;
	hp.split=4;
	hp.split_ind=0x18;
	hp.type=BIG_ENDIAN|DATA_INDIRECT|USING_SPLIT|SPLIT_INDIRECT;
	hp.length_offset=1;*/

	hp.addr=FindCallAndEntryAbs((DWORD)GetTextMetricsA,module_limit-module_base,module_base,0xFF6ACCCC);
	if (hp.addr==0) return;
	hp.addr+=2;
	hp.off=8;
	hp.split=-0x10;
	hp.length_offset=1;
	hp.type=BIG_ENDIAN|USING_SPLIT;
	NewHook(hp,L"CatSystem2");
	RegisterEngineType(ENGINE_CATSYSTEM);
}
void InsertNitroPlusHook()
{
	BYTE ins[]={0xB0,0x74,0x53};
	DWORD addr=SearchPattern(module_base,module_limit-module_base,ins,3);
	if (addr==0) return;
	addr+=module_base;
	ins[0]=*(BYTE*)(addr+3)&3;
	while (*(WORD*)addr!=0xEC83) addr--;
	HookParam hp={addr};
	hp.off=-0x14+(ins[0]<<2);
	hp.length_offset=1;
	hp.type|=BIG_ENDIAN;
	NewHook(hp,L"NitroPlus");
	RegisterEngineType(ENGINE_NITROPLUS);
}
void InsertRetouchHook()
{
	HookParam hp={};
	if (GetFunctionAddr("?printSub@RetouchPrintManager@@AAE_NPBDAAVUxPrintData@@K@Z",&hp.addr,0,0,0))
	{
		hp.off=4;
		hp.type=USING_STRING;
		NewHook(hp,L"RetouchSystem");
		RegisterEngineType(ENGINE_RETOUCH);
	}
	else if (GetFunctionAddr("?printSub@RetouchPrintManager@@AAEXPBDKAAH1@Z",&hp.addr,0,0,0))
	{
		hp.off=4;
		hp.type=USING_STRING;
		NewHook(hp,L"RetouchSystem");
		RegisterEngineType(ENGINE_RETOUCH);
	}
}
/********************************************************************************************
Malie hook:
	Process name is malie.exe.
	This is the most complicate code I have made. Malie engine store text string in
	linked list. We need to insert a hook to where it travels the list. At that point
	EBX should point to a structure. We can find character at -8 and font size at +10.
	Also need to enable ITH suppress function.
********************************************************************************************/
void InsertMalieHook()
{
	DWORD sig1=0x5E3C1;
	DWORD sig2=0xC383;
	DWORD i,j;
	i=SearchPattern(module_base,module_limit-module_base,&sig1,3);
	if (i==0) return;
	j=i+module_base+3;
	i=SearchPattern(j,module_limit-j,&sig2,2);
	if (j==0) return;
	HookParam hp={};
	hp.addr=j+i;
	hp.off=-0x14;
	hp.ind=-0x8;
	hp.split=-0x14;
	hp.split_ind=0x10;
	hp.length_offset=1;
	hp.type=USING_UNICODE|USING_SPLIT|DATA_INDIRECT|SPLIT_INDIRECT;
	NewHook(hp,L"Malie");
	RegisterEngineType(ENGINE_MALIE);
}
/********************************************************************************************
EMEHook hook: (Contributed by Freaka)
	EmonEngine is used by LoveJuice company and TakeOut. Earlier builds were apparently 
	called Runrunengine. String parsing varies alot depending on the font settings and 
	speed setting. E.g. without antialiasing (which very early versions did not have)
	uses TextOutA, fast speed triggers different functions then slow/normal. The user can
	set his own name and some odd control characters are used (0x09 for line break, 0x0D 
	for paragraph end) which is parsed and put together on-the-fly while playing so script
	can't be read directly. 
********************************************************************************************/
void InsertEMEHook()
{
	DWORD c=FindCallOrJmpAbs((DWORD)IsDBCSLeadByte,module_limit-module_base,(DWORD)module_base,false);
	
	/* no needed as first call to IsDBCSLeadByte is correct, but sig could be used for further verification 
	WORD sig = 0x51C3;
	while (c && (*(WORD*)(c-2)!=sig))
	{
		//-0x1000 as FindCallOrJmpAbs always uses an offset of 0x1000
		c=FindCallOrJmpAbs((DWORD)IsDBCSLeadByte,module_limit-c-0x1000+4,c-0x1000+4,false);
	} */

	if (c)
	{
		HookParam hp={};
		hp.addr=c;
		hp.off=-0x8;
		hp.length_offset=1;
		hp.type=NO_CONTEXT|DATA_INDIRECT;
		NewHook(hp,L"EmonEngine");
		OutputConsole(L"EmonEngine, hook will only work with text speed set to slow or normal!");
	}
	else OutputConsole(L"Unknown EmonEngine engine");
}
void SpecialRunrunEngine(DWORD esp_base, const HookParam& hp, DWORD* data, DWORD* split, DWORD* len)
{
	DWORD p1=*(DWORD*)(esp_base-0x8)+*(DWORD*)(esp_base-0x10); //eax+edx
	*data=*(WORD*)(p1);
	*len=2;
}
void InsertRREHook()
{
	DWORD c=FindCallOrJmpAbs((DWORD)IsDBCSLeadByte,module_limit-module_base,(DWORD)module_base,false);
	if (c)	
	{
		WORD sig = 0x51C3;
		
		HookParam hp={};
		hp.addr=c;	
		hp.length_offset=1;
		hp.type=NO_CONTEXT|DATA_INDIRECT;
		if ((*(WORD*)(c-2)!=sig)) 
		{
			hp.extern_fun=(DWORD)SpecialRunrunEngine;
			hp.type|=EXTERN_HOOK;
			NewHook(hp,L"RunrunEngine Old");
		} 
		else
		{		
			hp.off=-0x8;	
			NewHook(hp,L"RunrunEngine");
		}
		OutputConsole(L"RunrunEngine, hook will only work with text speed set to slow or normal!");
	}
	else OutputConsole(L"Unknown RunrunEngine engine");
}
void InsertMEDHook()
{
	DWORD i,j,k,t;
	for (i = module_base; i<module_limit - 4; i++)
	{
		if (*(DWORD*)i == 0x8175) //cmp *, 8175
		{
			for (j = i, k = i + 0x100; j < k; j++)
			{
				if (*(BYTE*)j == 0xE8)
				{
					t = j + 5 + *(DWORD*)(j+1);
					if (t > module_base && t < module_limit)
					{
						HookParam hp = {};
						hp.addr = t;
						hp.off = -0x8;
						hp.length_offset = 1;
						hp.type = BIG_ENDIAN;
						NewHook(hp, L"MED");
						//RegisterEngineType(ENGINE_MED);
						return;
					}
				}
			}
		}
	}
	OutputConsole(L"Unknown MED engine.");
}
static DWORD furi_flag;
void SpecialHookMalie(DWORD esp_base, const HookParam& hp, DWORD* data, DWORD* split, DWORD* len)
{
	DWORD index,ch,ptr;
	ch=*(DWORD*)(esp_base-0x8)&0xFFFF;
	ptr=*(DWORD*)(esp_base-0x24);
	*data=ch;
	*len=2;
	if (furi_flag)
	{
		index=*(DWORD*)(esp_base-0x10);
		if (*(WORD*)(ptr+index*2-2)<0xA)
			furi_flag=0;
	}
	else if (ch==0xA)
	{
		furi_flag=1;
		len=0;
	}
	*split=furi_flag;	
}
void InsertMalieHook2()
{
	BYTE ins[4]={0x66,0x3D,0x1,0};
	DWORD p;
	BYTE* ptr;
	p=SearchPattern(module_base,module_limit-module_base,ins,4);
	if (p)
	{
		ptr=(BYTE*)(p+module_base);
_again:
		if (*(WORD*)ptr==0x3D66)
		{		
			ptr+=4;
			if (ptr[0]==0x75) {ptr+=ptr[1]+2;goto _again;}
			if (*(WORD*)ptr==0x850F) {ptr+=*(DWORD*)(ptr+2)+6;goto _again;}
		}
		HookParam hp={};
		hp.addr=(DWORD)ptr+4;
		hp.off=-8;
		hp.length_offset=1;
		hp.extern_fun=(DWORD)SpecialHookMalie;
		hp.type=EXTERN_HOOK|USING_SPLIT|USING_UNICODE|NO_CONTEXT;
		NewHook(hp,L"Malie");
		RegisterEngineType(ENGINE_MALIE);
		return;
	}
	OutputConsole(L"Unknown malie system.");
}
/********************************************************************************************
AbelSoftware hook:
	The game folder usually is made up many no extended name files(file name doesn't have '.').
	And these files have common prefix which is the game name, and 2 digit in order.


********************************************************************************************/
void InsertAbelHook()
{
	DWORD character[2]={0xC981D48A,0xFFFFFF00};
	DWORD i,j=SearchPattern(module_base,module_limit-module_base,character,8);
	if (j)
	{
		j+=module_base;
		for (i=j-0x100;j>i;j--)
		{
			if (*(WORD*)j==0xFF6A)
			{
				HookParam hp={};
				hp.addr=j;
				hp.off=4;
				hp.type=USING_STRING|NO_CONTEXT;
				NewHook(hp,L"AbelSoftware");
				RegisterEngineType(ENGINE_ABEL);
				return;
			}
		}
	}
}
bool InsertLiveDynamicHook(LPVOID addr, DWORD frame, DWORD stack)
{
	if (addr!=GetGlyphOutlineA) return false;
	DWORD i,j,k;
	HookParam hp={};
	i=frame;
	if (i!=0)
	{
		k=*(DWORD*)i;
		k=*(DWORD*)(k+4);
		if (*(BYTE*)(k-5)!=0xE8)
			k=*(DWORD*)(i+4);
		j=k+*(DWORD*)(k-4);
		if (j>module_base&&j<module_limit)
		{
			hp.addr=j;
			hp.off=-0x10;
			hp.length_offset=1;
			hp.type|=BIG_ENDIAN;
			NewHook(hp,L"Live");
			RegisterEngineType(ENGINE_LIVE);
			return true;
		}
	}
	return true;
}
/*void InsertLiveHook()
{
	OutputConsole(L"Probably Live. Wait for text.");
	SwitchTrigger();
	trigger_fun=InsertLiveDynamicHook;
}*/
void InsertLiveHook()
{
	BYTE sig[7]={0x64,0x89,0x20,0x8B,0x45,0x0C,0x50};
	DWORD i=SearchPattern(module_base,module_limit-module_base,sig,7);
	if (i)
	{
		HookParam hp={};
		hp.addr=i+module_base;
		hp.off=-0x10;
		hp.length_offset=1;
		hp.type|=BIG_ENDIAN;
		NewHook(hp,L"Live");
		RegisterEngineType(ENGINE_LIVE);
	}
	else OutputConsole(L"Unknown Live engine");
}
void InsertBrunsHook()
{
	HookParam hp={};
	hp.off=4;
	hp.length_offset=1;
	hp.type=USING_UNICODE|MODULE_OFFSET|FUNCTION_OFFSET;
	hp.function=0x8B24C7BC;
	//?push_back@?$basic_string@GU?$char_traits@G@std@@V?$allocator@G@2@@std@@QAEXG@Z
	if (IthCheckFile(L"msvcp90.dll"))
	{
		hp.module=0xC9C36A5B; //msvcp90.dll
		NewHook(hp,L"Bruns");
		RegisterEngineType(ENGINE_BRUNS);
		return;
	}
	if (IthCheckFile(L"msvcp80.dll"))
	{
		hp.module=0xA9C36A5B; //msvcp80.dll
		NewHook(hp,L"Bruns");
		RegisterEngineType(ENGINE_BRUNS);
		return;
	}
}
void SpecialHookFrontwing(DWORD esp_base, const HookParam& hp, DWORD* data, DWORD* split, DWORD* len)
{
	char* msg=*(char**)(esp_base-0xC);
	char* text=*(char**)(esp_base-0x14);
	if (text)
	{
		BYTE c=text[0];
		if (c!='^'&&msg)
		{
			msg+=8;
			if (strcmp(msg,"MessageAction,self")==0)
			//if (strcmp(msg,"\\sub,@@!MessageAddLog,self")==0)
			{
				while (1)
				{
					c=text[0];
					if (c==0) break;
					if (LeadByteTable[c]==2) break;
					text++;
				}
				*len=strlen(text);
				*data=(DWORD)text;
				*split=*(DWORD*)(esp_base-0x18);
				return;
			}
		}
	}
	*len=0;
}
void InsertFrontwingHook()
{

	/*BYTE sig[8]={0x6A,0,0x6A,0,0x6A,0,0x6A,0};

	DWORD i=SearchPattern(module_base,module_limit-module_base,sig,8);*/
	DWORD i,j,t;
	for (i = module_base + 0x1000; i < module_limit - 4; i++ )
	{
		if (*(DWORD*)i == 0x7FFE8347) //inc edi, cmp esi,7f
		{
			t = 0;
			for (j = i; j < i + 0x10; j++)
			{
				if (*(DWORD*)j == 0xA0) //cmp esi,a0
				{
					t = 1;
					break;
				}
			}
			if (t)
			{
				for (j = i; j > i - 0x100; j--)
				{
					if (*(DWORD*)j == 0x83EC8B55) //push ebp, mov ebp,esp, sub esp,*
					{
						HookParam hp = {};
						hp.addr = j;
						hp.off = 0x18;
						hp.length_offset = 1;
						hp.type = DATA_INDIRECT;
						NewHook(hp,L"QLIE");
						RegisterEngineType(ENGINE_FRONTWING);
						return;
					}
				}
			}
		}
	}
	/*if (i)
	{
		HookParam hp={};
		hp.addr=module_base+i-3;
		hp.extern_fun=(DWORD)SpecialHookFrontwing;
		hp.type=EXTERN_HOOK|USING_STRING;
		NewHook(hp,L"QLIE");
		RegisterEngineType(ENGINE_FRONTWING);
	}*/
	OutputConsole(L"Unknown QLIE engine");
}
/********************************************************************************************
CandySoft hook:
	Game folder contains many *.fpk. Engine name is SystemC.
	I haven't seen this engine in other company/brand.

	AGTH /X3 will hook lstrlenA. One thread is the exactly result we want.
	But the function call is difficult to located programmatically.
	I find a equivalent points which is more easy to search.
	The script processing function needs to find 0x5B'[',
	so there should a instruction like cmp reg,5B
	Find this position and navigate to function entry.
	The first parameter is the string pointer.
	This approach works fine with game later than つよきす２学期.

	But the original つよきす is quite different. I handle this case separately.

********************************************************************************************/
void InsertCandyHook()
{
	DWORD i,j,k;
	if (_wcsicmp(process_name,L"systemc.exe")==0)
	{
		for (i=module_base+0x1000;i<module_limit-4;i++)
		{
			if ((*(DWORD*)i&0xFFFFFF)==0x24F980) //cmp cl,24
			{
				for (k = i - 100, j = i; j > k; j--)
				{
					if (*(DWORD*)j==0xC0330A8A) //mov cl,[edx];xor eax,eax
					{
						HookParam hp={};
						hp.addr=j;
						hp.off=-0x10;
						hp.type=USING_STRING;
						NewHook(hp,L"SystemC");
						RegisterEngineType(ENGINE_CANDY);
						return;
					}
				}
			}
		}
	}
	else
	for (i=module_base+0x1000;i<module_limit-4;i++)
	{
		if (*(WORD*)i==0x5B3C|| //cmp al,0x5B
			(*(DWORD*)i&0xFFF8FC)==0x5BF880) //cmp reg,0x5B
		{
			for (j=i,k=i-0x100;j>k;j--)
			{
				if ((*(DWORD*)j&0xFFFF)==0x8B55) //push ebp, mov ebp,esp, sub esp,*
				{
					HookParam hp={};
					hp.addr=j;
					hp.off=4;
					hp.type=USING_STRING;
					NewHook(hp,L"SystemC");
					RegisterEngineType(ENGINE_CANDY);
					return;
				}
			}
		}
	}
	OutputConsole(L"Unknown CandySoft engine.");
}
/********************************************************************************************
Apricot hook:
	Game folder contains arc.a*.
	This engine is heavily based on new DirectX interfaces.
	I can't find a good place where text is clean and not repeating.
	The game process script encoded in UTF-32 like format.
	I reversed the parsing algorithm of the game and implement it partially.
	Only name and text data is needed. 

********************************************************************************************/
void SpecialHookApricot(DWORD esp_base, const HookParam& hp, DWORD* data, DWORD* split, DWORD* len)
{
	DWORD reg_esi=*(DWORD*)(esp_base-0x20);
	DWORD reg_esp=*(DWORD*)(esp_base-0x18);
	DWORD base=*(DWORD*)(reg_esi+0x24);
	DWORD index=*(DWORD*)(reg_esi+0x3C);
	DWORD *script=(DWORD*)(base+index*4),*end;
	*split=reg_esp;
	if (script[0]==L'<')
	{
		for (end=script;*end!=L'>';end++);
		if (script[1]==L'N')
		{
			if (script[2]==L'a'&&script[3]==L'm'&&script[4]==L'e')
			{
				buffer_index=0;
				for (script+=5;script<end;script++)
					if (*script>0x20)
						wc_buffer[buffer_index++]=*script&0xFFFF;
				*len=buffer_index<<1;
				*data=(DWORD)wc_buffer;
				*split|=1<<31;
			}
		}
		else if (script[1]==L'T')
		{
			if (script[2]==L'e'&&script[3]==L'x'&&script[4]==L't')
			{
				buffer_index=0;
				for (script+=5;script<end;script++)
				{
					if (*script>0x40)
					{
						while (*script==L'{')
						{	
							script++;
							while (*script!=L'\\')
							{
								wc_buffer[buffer_index++]=*script&0xFFFF;
								script++;
							}
							while (*script++!=L'}');
						}				
						wc_buffer[buffer_index++]=*script&0xFFFF;
					}
				}
				*len=buffer_index<<1;
				*data=(DWORD)wc_buffer;
			}
		}
	}

}
void InsertApricotHook()
{
	DWORD i,j,k;
	for (i=module_base+0x1000;i<module_limit-4;i++)
	{
		if ((*(DWORD*)i&0xFFF8FC)==0x3CF880) //cmp reg,0x3C
		{
			j=i+3;
			for (k=i+0x100;j<k;j++)
			{
				if ((*(DWORD*)j&0xFFFFFF)==0x4C2) //retn 4
				{
					HookParam hp={};
					hp.addr=j+3;
					hp.extern_fun=(DWORD)SpecialHookApricot;
					hp.type=EXTERN_HOOK|USING_STRING|USING_UNICODE|NO_CONTEXT;
					NewHook(hp,L"ApRicot");
					RegisterEngineType(ENGINE_APRICOT);
					return;
				}
			}
		}
	}
}
void InsertStuffScriptHook()
{
	HookParam hp = {};
	hp.addr = (DWORD)GetTextExtentPoint32A;
	hp.off = 8;
	hp.split = -0x18;
	hp.type = USING_STRING | USING_SPLIT;
	NewHook(hp, L"StuffScriptEngine");
	//RegisterEngine(ENGINE_STUFFSCRIPT);
}
void InsertTriangleHook()
{
	DWORD i,j,k,t;
	for (i = module_base; i < module_limit - 4; i++)
	{
		if ((*(DWORD*)i & 0xFFFFFF) == 0x75403C) // cmp al,0x40; jne
		{
			j = i + 4 + *(BYTE*)(i+3);
			for (k = j + 0x20; j < k; j++)
			{
				if (*(BYTE*)j == 0xE8)
				{
					t = j + 5 + *(DWORD*)(j+1);
					if (t > module_base && t < module_limit)
					{
						HookParam hp = {};
						hp.addr = t;
						hp.off = 4;
						hp.type = USING_STRING;
						NewHook(hp, L"Triangle");
						//RegisterEngineType(ENGINE_TRIANGLE);
						return;
					}
				}
			}

		}
	}
	OutputConsole(L"Old/Unknown Triangle engine.");
}
void InsertPensilHook()
{
	DWORD i,j;
	for (i = module_base; i < module_limit - 4; i++)
	{
		if (*(DWORD*)i == 0x63813D) //cmp eax,8163
		{
			j = FindEntryAligned(i,0x100);
			if (j)
			{
				HookParam hp = {};
				hp.addr = j;
				hp.off = 8;
				hp.length_offset = 1;
				NewHook(hp,L"Pencil");
				//RegisterEngineType(ENGINE_PENSIL);
			}
		}
	}
	OutputConsole(L"Unknown Pensil engine.");
}
void SpecialHookDebonosu(DWORD esp_base, HookParam& hp, DWORD* data, DWORD* split, DWORD* len)
{
	DWORD retn = *(DWORD*)esp_base;
	if (*(WORD*)retn == 0xC483) //add esp, *
		hp.off = 4;
	else
		hp.off = -0x8;
	hp.type ^= EXTERN_HOOK;
	hp.extern_fun = 0;
	*data = *(DWORD*)(esp_base + hp.off);
	*len = strlen((char*)*data);
}
DWORD FindImportEntry(DWORD hModule, DWORD fun)
{
	IMAGE_DOS_HEADER *DosHdr;
	IMAGE_NT_HEADERS *NtHdr;
	DWORD IAT,end,pt,addr;
	DosHdr = (IMAGE_DOS_HEADER*)hModule;
	if (IMAGE_DOS_SIGNATURE == DosHdr -> e_magic)
	{
		NtHdr = (IMAGE_NT_HEADERS*)(hModule+DosHdr -> e_lfanew);
		if (IMAGE_NT_SIGNATURE == NtHdr -> Signature)
		{
			IAT = NtHdr -> OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT].VirtualAddress;
			end = NtHdr -> OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT].Size;
			IAT += hModule;
			end += IAT;
			for (pt = IAT; pt < end; pt+= 4)
			{
				addr = *(DWORD*)pt;
				if (addr == fun) return pt;
			}
		}
	}
	return 0;
}
void InsertDebonosuHook()
{
	DWORD fun,addr,search,i,j,k,push;
	if (GetFunctionAddr("lstrcatA",&fun,0,0,0) == 0) return;
	addr = FindImportEntry(module_base,fun);
	if (addr == 0) return;
	search = 0x15FF | (addr << 16);
	addr >>= 16;
	for (i = module_base; i < module_limit - 4; i++)
	{
		if (*(DWORD*)i != search) continue;
		if (*(WORD*)(i + 4) != addr) continue;// call dword ptr lstrcatA
		if (*(BYTE*)(i - 5) != 0x68) continue;// push $
		push = *(DWORD*)(i - 4);
		j = i + 6;
		for (k = j + 0x10; j < k; j++)
		{
			if (*(BYTE*)j != 0xB8) continue;
			if (*(DWORD*)(j + 1) != push) continue;
			HookParam hp = {};
			hp.addr = FindEntryAligned(i, 0x200);
			hp.extern_fun = (DWORD)SpecialHookDebonosu;
			if (hp.addr == 0) continue;
			hp.type = USING_STRING | EXTERN_HOOK;
			NewHook(hp, L"Debonosu");
			//RegisterEngineType(ENGINE_DEBONOSU);
			return;
		}
	}
	OutputConsole(L"Unknown Debonosu engine.");
}
void SpecialHookSofthouse(DWORD esp_base, const HookParam& hp, DWORD* data, DWORD* split, DWORD* len)
{
	DWORD i;
	union
	{
		LPWSTR string_u;
		PCHAR string_a;
	};
	string_u=*(LPWSTR*)(esp_base+4);
	if (hp.type&USING_UNICODE)
	{
		*len=wcslen(string_u);
		for (i=0;i<*len;i++)
		{
			if (string_u[i]==L'>'||string_u[i]==L']')
			{
				*data=(DWORD)(string_u+i+1);
				*split=0;
				*len-=i+1;
				*len<<=1;
				return;
			}
		}
	}
	else
	{
		*len=strlen(string_a);
		for (i=0;i<*len;i++)
		{
			if (string_a[i]=='>'||string_a[i]==']')
			{
				*data=(DWORD)(string_a+i+1);
				*split=0;
				*len-=i+1;
				return;
			}
		}
	}
}
bool InsertSofthouseDynamicHook(LPVOID addr, DWORD frame, DWORD stack)
{
	if (addr!=DrawTextExA&&addr!=DrawTextExW) return false;
	DWORD high,low,i,j,k;
	GetCodeRange(module_base,&low,&high);
	i=stack;
	j=(i&0xFFFF0000)+0x10000;
	for (;i<j;i+=4)
	{
		k=*(DWORD*)i;
		if (k>low&&k<high)
		{
			if ((*(WORD*)(k-6)==0x15FF)||*(BYTE*)(k-5)==0xE8)
			{				
				HookParam hp={};
				hp.off=0x4;
				hp.extern_fun=(DWORD)SpecialHookSofthouse;
				hp.type=USING_STRING|EXTERN_HOOK;
				if (addr==DrawTextExW) {hp.type|=USING_UNICODE;}
				i=*(DWORD*)(k-4);
				if (*(DWORD*)(k-5)==0xE8)
					hp.addr=i+k;
				else
					hp.addr=*(DWORD*)i;
				NewHook(hp,L"SofthouseChara");
				RegisterEngineType(ENGINE_SOFTHOUSE);
				return true;
			}
		}
	}
	OutputConsole(L"Fail to insert hook for SofthouseChara.");
	return true;
}
void InsertSoftHouseHook()
{
	OutputConsole(L"Probably SoftHouseChara. Wait for text.");
	SwitchTrigger();
	trigger_fun=InsertSofthouseDynamicHook;
}
void SpecialHookCaramelBox(DWORD esp_base, const HookParam& hp, DWORD* data, DWORD* split, DWORD* len)
{
	DWORD reg_ecx = *(DWORD*)(esp_base+hp.off);
	BYTE* ptr = (BYTE*)reg_ecx;
	buffer_index = 0;
	while (ptr[0])
	{
		if (ptr[0] == 0x28) // Furigana format: (Kanji,Furi)
		{
			ptr++;
			while (ptr[0]!=0x2C) //Copy Kanji
				text_buffer[buffer_index++] = *ptr++;
			while (ptr[0]!=0x29) // Skip Furi
				ptr++;
			ptr++;
		}
		else if (ptr[0] == 0x5C) ptr +=2;
		else
		{
			text_buffer[buffer_index++] = ptr[0];
			if (LeadByteTable[ptr[0]]==2)
			{
				ptr++;
				text_buffer[buffer_index++] = ptr[0];
			}
			ptr++;
		}
	}
	*len = buffer_index;
	*data = (DWORD)text_buffer;
	*split = 0;
}
void InsertCaramelBoxHook()
{
	DWORD j,k,flag,reg;
	union {DWORD i; BYTE* pb; WORD* pw; DWORD* pd;};
	reg = -1;
	for (i = module_base + 0x1000; i < module_limit - 4; i++)
	{
		
		if (*pd == 0x7FF3D) //cmp eax, 7FF
			reg = 0;
		else if ((*pd & 0xFFFFF8FC) == 0x07FFF880) //cmp reg, 7FF
			reg = pb[1] & 0x7;

		if (reg == -1) continue;

		flag = 0;
		if (*(pb - 6) == 3) //add reg, [ebp+$disp_32]
		{
			if (*(pb - 5) == (0x85 | (reg << 3)))
				flag = 1;
		}
		else if (*(pb - 3) == 3) //add reg, [ebp+$disp_8]
		{
			if (*(pb - 2) == (0x45 | (reg << 3)))
				flag = 1;
		}
		else if (*(pb - 2) == 3) //add reg, reg
		{
			if (((*(pb - 1) >> 3) & 7)== reg) 
				flag = 1;
		}
		reg = -1;
		if (flag)
		{
			for (j = i, k = i - 0x100; j > k; j-- )
			{
				if ((*(DWORD*)j&0xFFFF00FF)==0x1000B8) //mov eax,10??
				{
					HookParam hp = {};
					hp.addr = j & ~0xF;
					hp.extern_fun = (DWORD)SpecialHookCaramelBox;
					hp.type = USING_STRING | EXTERN_HOOK;
					for (i &= ~0xFFFF; i < module_limit - 4; i++)
					{
						if (pb[0] == 0xE8)
						{
							pb++;
							if (pd[0] + i + 4 == hp.addr)
							{
								pb += 4;
								if ((pd[0] & 0xFFFFFF) == 0x04C483)
									hp.off = 4;
								else hp.off = -0xC;
								break;
							}
						}
					}
					if (hp.off == 0) goto _unknown_engine;
					NewHook(hp, L"CaramelBox");
					RegisterEngineType(ENGINE_CARAMEL);
					return;
				}
			}
		}
	}
_unknown_engine:
	OutputConsole(L"Unknown CarmelBox engine.");
}
void InsertWolfHook()
{
	__asm int 3
	DWORD c1 = FindCallAndEntryAbs((DWORD)GetTextMetricsA,module_limit-module_base,module_base,0xEC81);
	if (c1)
	{
		DWORD c2 = FindCallOrJmpRel(c1,module_limit-module_base,module_base,0);
		if (c2)
		{
			union {DWORD i; WORD *k;};
			DWORD j;
			for (i = c2 - 0x100, j = c2 - 0x400; i > j; i--)
			{
				if (*k == 0xEC83)
				{
					HookParam hp = {};
					hp.addr = i;
					hp.off = -0xC;
					hp.split = -0x18;
					hp.type = DATA_INDIRECT | USING_SPLIT;
					hp.length_offset = 1;
					NewHook(hp, L"WolfRPG");
					return;
				}
			}
		}
	}
	
	OutputConsole(L"Unknown WolfRPG engine.");
	return;
}
bool InsertIGSDynamicHook(LPVOID addr, DWORD frame, DWORD stack)
{
	if (addr!=GetGlyphOutlineW) return false;
	HookParam hp={};
	DWORD i,j,k,t;
	i=*(DWORD*)frame;
	i=*(DWORD*)(i+4);
	if (FillRange(L"mscorlib.ni.dll",&j,&k))
	{
		while (*(BYTE*)i!=0xE8) i++;
		t=*(DWORD*)(i+1)+i+5;
		if (t>j&&t<k)
		{
			hp.addr=t;
			hp.off=-0x10;
			hp.split=-0x18;
			hp.length_offset=1;
			hp.type=USING_UNICODE|USING_SPLIT;
			NewHook(hp,L"IronGameSystem");
			OutputConsole(L"IGS - Please set text(テキスト) display speed(表示速度) to fastest(瞬間)");
			RegisterEngineType(ENGINE_IGS);
			return true;
		}
	}
	return true;
}
void InsertIronGameSystemHook()
{
	OutputConsole(L"Probably IronGameSystem. Wait for text.");
	SwitchTrigger();
	trigger_fun=InsertIGSDynamicHook;
}
/*
void SpecialHookDotNet(DWORD esp_base, const HookParam& hp, DWORD* data, DWORD* split, DWORD* len)
{
	DWORD ptr=*(DWORD*)(esp_base-0x10);
	*len=*(DWORD*)(ptr+8)<<1;
	*data=ptr+0xC;
	*split=0;
}
bool InsertDotNetDynamicHook(LPVOID addr, DWORD frame, DWORD stack)
{
	if (trigger_addr!=addr) return false;
	DWORD ptr=*(DWORD*)frame;
	ptr=*(DWORD*)(ptr+4);
	if (*(WORD*)(ptr-6)==0x15FF)
	{
		HookParam hp={};
		ptr=*(DWORD*)(ptr-4);
		hp.addr=*(DWORD*)(ptr);
		hp.extern_fun=(DWORD)SpecialHookDotNet;
		hp.type=USING_UNICODE|EXTERN_HOOK|USING_STRING;
		NewHook(hp,L"DotNet2");
		RegisterEngineType(ENGINE_DOTNET1);
		return true;
	}
	return false;
}
void InsertDotNetHook1(DWORD module, DWORD module_limit)
{
	DWORD sig=0xC768D,size,i,j;
	size=module_limit-module-4;
	i=SearchPattern(module,size,&sig,3);
	if (i)
	{
		j=SearchPattern(module+i+4,size-i-4,&sig,3);
		if (j)
		{
			HookParam hp={};
			hp.addr=module+i+j+7;
			hp.off=-0x20;
			hp.split=0x18;
			hp.type=USING_UNICODE|USING_STRING|NO_CONTEXT|USING_SPLIT;
			NewHook(hp,L"DotNet1");
			SwitchTrigger();
			trigger_addr=(LPVOID)hp.addr;
			trigger_fun=InsertDotNetDynamicHook;
		}
	}
}
*/
int cmp(const void * a, const void * b)
{
	return *(int*)a - *(int*)b;
}
/********************************************************************************************
AkabeiSoft2Try hook:
	Game folder contains GameLib.dll.
	This engine is based on .NET framework. This really makes it troublesome to locate a
	valid hook address. The problem is that the engine file merely contains bytecode for
	the CLR. Real meaningful object code is generated dynamicaly and the address is randomized.
	Therefore the easiest method is to brute force search whole address space. While it's not necessary 
	to completely search the whole address space, since non-executable pages can be excluded first.
	The generated code sections do not belong to any module(exe/dll), hence they do not have 
	a section name. So we can also exclude executable pages from all modules. At last, the code
	section should be long(>0x2000). The remain address space should be several MB and can be
	examined in reasonable time(less than 0.1s for P8400 Win7x64).
	Characteristic sequence is 0F B7 44 50 0C, stands for movzx eax, word ptr [edx*2 + eax + C].
	Obviously this instruction extracts one unicode character from a string.
	A main shortcoming is that the code is not generated if it's not being used.
	So if you are in title screen this approach will fail.

********************************************************************************************/
MEMORY_WORKING_SET_LIST* GetWorkingSet()
{
	DWORD len,retl,s;
	NTSTATUS status;
	LPVOID buffer = 0; 
	len = 0x4000;
	status = NtAllocateVirtualMemory(NtCurrentProcess(), &buffer, 0, &len, MEM_COMMIT, PAGE_READWRITE);
	if (!NT_SUCCESS(status)) return 0;
	status = NtQueryVirtualMemory(NtCurrentProcess(), 0, MemoryWorkingSetList, buffer, len, &retl);
	if (status == STATUS_INFO_LENGTH_MISMATCH)
	{
		len = *(DWORD*)buffer;
		len = ((len << 2) & 0xFFFFF000) + 0x4000;
		s = 0;
		NtFreeVirtualMemory(NtCurrentProcess(), &buffer, &s, MEM_RELEASE);
		buffer = 0;
		status = NtAllocateVirtualMemory(NtCurrentProcess(), &buffer, 0, &len, MEM_COMMIT, PAGE_READWRITE);
		if (!NT_SUCCESS(status)) return 0;
		status = NtQueryVirtualMemory(NtCurrentProcess(), 0, MemoryWorkingSetList, buffer, len, &retl);
		if (!NT_SUCCESS(status)) return 0;
	}
	return (MEMORY_WORKING_SET_LIST*)buffer;
}
void ParseWorkingSet(MEMORY_WORKING_SET_LIST* list)
{
	NTSTATUS status;
	WCHAR path[MAX_PATH];
	DWORD count = 0, i, retl, addr;
	for (i = 0; i < list->NumberOfPages; i++)
	{
		//High 20 bit is page address. Low 12 bit is a set of flags.
		addr = list->WorkingSetList[i] & ~0xFFF;

		//On WinXP working set contains address higher than 0x80000000 which belongs to kernel space.
		//Some of those pages is also marked executable but they can't be read directly from user space.
		//So we must skip them first, otherwise access violation may occur in following procedure.
		if (addr >> 31) continue; 

		if (list->WorkingSetList[i] & 2) //WSLE_PAGE_EXECUTE
		{
			//Select executable sections without a name, i.e. those are not part of exe or dll.
			status = NtQueryVirtualMemory(NtCurrentProcess(),(PVOID)addr,MemorySectionName,path,MAX_PATH<<1,&retl);
			if (NT_SUCCESS(status)) continue;
			list->WorkingSetList[count++] = addr;
		}
	}
	qsort(&list->WorkingSetList, count, 4, cmp);
	list->NumberOfPages = count;
};
void MergeCodeSection(MEMORY_WORKING_SET_LIST* list)
{
	//Merge pages into sections.
	CodeSection *ps,*pe,*pt; 
	DWORD i;
	ps = code_section;
	ps->base = list->WorkingSetList[0];
	ps->size = 0x1000;
	for (i = 1; i < list->NumberOfPages; i++)
	{
		if (ps->base + ps->size == list->WorkingSetList[i])
		{
			ps->size += 0x1000;
		}
		else
		{
			ps++;
			ps->base = list->WorkingSetList[i];
			ps->size = 0x1000;
		}
	}
	//Selection sections bigger than 0x2000
	ps++;
	pe = code_section;
	for (pt = code_section; pt != ps; pt++)
	{
		if (pt->size>0x2000)
		{
			pe->base = pt->base;
			pe->size = pt->size;
			pe++;
		}
	}
	pe->base = 0;
	pe->size = 0;
}
void SpecialHookAB2Try(DWORD esp_base, const HookParam& hp, DWORD* data, DWORD* split, DWORD* len)
{
	DWORD test = *(DWORD*)(esp_base - 0x10);
	if (test != 0) return;
	DWORD ptr = *(DWORD*)(esp_base - 0x8);
	*len = *(DWORD*)(ptr + 8) << 1;
	*data = ptr + 0xC;
	*split = 0;
}
void InsertAB2TryHook()
{
	IthBreak();
	//LARGE_INTEGER fre,beg,end;
	//NtQueryPerformanceCounter(&beg,&fre);
	MEMORY_WORKING_SET_LIST* list = GetWorkingSet();
	if (list == 0) return;
	ParseWorkingSet(list);
	MergeCodeSection(list);
	CodeSection *ps;
	DWORD i,j;
	for (ps = code_section; ps->base; ps++)
	{
		j = ps->base + ps->size - 4;
		for (i = ps->base; i < j; i++)
		{
			if (*(DWORD*)i == 0x5044B70F)
			{
				if (*(WORD*)(i + 4) == 0x890C) //movzx eax, word ptr [edx*2 + eax + 0xC]; wchar = string[i];
				{
					HookParam hp = {};
					hp.addr = i;
					hp.extern_fun = (DWORD)SpecialHookAB2Try;
					hp.type = USING_STRING | USING_UNICODE| EXTERN_HOOK | NO_CONTEXT;
					NewHook(hp,L"AB2Try");
					OutputConsole(L"Please adjust text speed to fastest/immediate.");
					//RegisterEngineType(ENGINE_AB2T);
					goto found_ab2t;
				}
			}
		}
	}
	OutputConsole(L"Can't find characteristic sequence. \
Make sure you have start the game and some text on the screen.");
found_ab2t:
	i = 0;
	//NtQueryPerformanceCounter(&end,0);
	//WCHAR search_time[0x40];
	//end.QuadPart-=beg.QuadPart;
	//swprintf(search_time,L"%.6lfs",(double)end.QuadPart/(double)fre.QuadPart);
	//OutputConsole(search_time);
	NtFreeVirtualMemory(NtCurrentProcess(), (PVOID*)&list, &i, MEM_RELEASE);
}
/********************************************************************************************
C4 hook: (Contributed by Stomp)
	Game folder contains C4.EXE or XEX.EXE.

********************************************************************************************/
void InsertC4Hook()
{
	BYTE sig[8]={0x8A, 0x10, 0x40, 0x80, 0xFA, 0x5F, 0x88, 0x15};
	DWORD i=SearchPattern(module_base,module_limit-module_base,sig,8);
	if (i)
	{
		HookParam hp={};
		hp.addr=i+module_base;
		hp.off=-0x08;
		hp.type|=DATA_INDIRECT|NO_CONTEXT;
		hp.length_offset=1;
		NewHook(hp,L"C4");
		//RegisterEngineType(ENGINE_C4);
	}
	else OutputConsole(L"Unknown C4 engine");
}
extern "C" int __declspec(dllexport) InsertDynamicHook(LPVOID addr, DWORD frame, DWORD stack)
{
	return !trigger_fun(addr,frame,stack);
}
DWORD GetModuleBase()
{
	__asm
	{
		mov eax,fs:[0x18]
		mov eax,[eax+0x30]
		mov eax,[eax+0xC]
		mov eax,[eax+0xC]
		mov eax,[eax+0x18]
	}
}
//Search string in rsrc section. This section usually contains version and copyright info.
bool SearchResourceString(LPWSTR str)
{
	DWORD hModule=GetModuleBase();
	IMAGE_DOS_HEADER *DosHdr;
	IMAGE_NT_HEADERS *NtHdr;
	DosHdr=(IMAGE_DOS_HEADER*)hModule;
	DWORD rsrc,size;
	//__asm int 3
	if (IMAGE_DOS_SIGNATURE==DosHdr->e_magic)
	{
		NtHdr=(IMAGE_NT_HEADERS*)(hModule+DosHdr->e_lfanew);
		if (IMAGE_NT_SIGNATURE==NtHdr->Signature)
		{
			rsrc=NtHdr->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_RESOURCE].VirtualAddress;
			if (rsrc)
			{
				rsrc+=hModule;
				if (IthGetMemoryRange((LPVOID)rsrc,&rsrc,&size))
					if (SearchPattern(rsrc,size-4,str,wcslen(str)<<1)) return true;
			}
		}
	}
	return false;
}
bool IsKiriKiriEngine()
{
	return SearchResourceString(L"TVP(KIRIKIRI)");
}
static BYTE static_file_info[0x1000];
DWORD DetermineEngineByFile1()
{
	if (IthFindFile(L"*.xp3")||IsKiriKiriEngine())
	{
		InsertKiriKiriHook();
		return 0;
	}
	if (IthFindFile(L"bgi.*"))
	{
		InsertBGIHook();
		return 0;
	}
	if (IthFindFile(L"data*.arc")&&IthCheckFile(L"stream.arc"))
	{
		InsertMajiroHook();
		return 0;
	}
	if (IthFindFile(L"data\\pack\\*.cpz"))
	{
		InsertCMVSHook();
		return 0;
	}
	if (IthCheckFile(L"message.dat"))
	{
		InsertAtelierHook();
		return 0;
	}

	if (IthCheckFile(L"advdata\\voice.dat"))
	{
		if (IthCheckFile(L"advdata\\dat\\names.dat"))
			InsertCircusHook();			
		else
			InsertCircusHook2();
		return 0;
	}
	if (IthFindFile(L"*.war"))
	{
		InsertShinaHook();
		return 0;
	}
	if (IthFindFile(L"*.noa"))
	{
		InsertCotophaHook();
		return 0;
	}
	if (IthFindFile(L"*.int"))
	{
		InsertCatSystem2Hook();
		return 0;
	}
	if (IthFindFile(L"GameData\\*.pack"))
	{
		InsertFrontwingHook();
		return 0;
	}

	/*if (IthFindFile(L"data.wolf"))
	{
		InsertWolfHook();
		return 0;
	}*/
	return 1;
}
DWORD DetermineEngineByFile2()
{
	if (IthFindFile(L"*.lpk"))
	{
		InsertLucifenHook();
		return 0;
	}
	if (IthCheckFile(L"cfg.pak"))
	{
		InsertWaffleHook();
		return 0;
	}
	if (IthCheckFile(L"Arc00.dat"))
	{
		InsertTinkerBellHook();
		return 0;
	}
	if (IthFindFile(L"*.vfs"))
	{
		InsertSoftHouseHook();
		return 0;
	}
	if (IthFindFile(L"*.mbl"))
	{
		InsertLuneHook();
		return 0;
	}
	if (IthFindFile(L"pac\\*.ypf"))
	{
		InsertWhirlpoolHook();
		return 0;
	}
	if (IthFindFile(L"*.npa"))
	{
		InsertNitroPlusHook();
		return 0;
	}
	if (IthCheckFile(L"resident.dll"))
	{
		InsertRetouchHook();
		return 0;
	}
	if (IthCheckFile(L"malie.ini"))
	{
		if (IthCheckFile(L"tools.dll"))
			InsertMalieHook();
		else
			InsertMalieHook2();	
		return 0;
	}
	if (IthCheckFile(L"live.dll"))
	{
		InsertLiveHook();
		return 0;
	}
	return 1;
}
DWORD DetermineEngineByFile3()
{
	if (IthCheckFile(L"libscr.dll"))
	{
		InsertBrunsHook();
		return 0;
	}
	if (IthFindFile(L"emecfg.ecf"))
	{
		InsertEMEHook();
		return 0;
	}
	if (IthFindFile(L"rrecfg.rcf"))
	{
		InsertRREHook();
		return 0;
	}
	if (IthFindFile(L"*.fpk"))
	{
		InsertCandyHook();
		return 0;
	}
	if (IthFindFile(L"arc.a*"))
	{
		InsertApricotHook();
		return 0;
	}
	if (IthFindFile(L"*.mpk"))
	{
		InsertStuffScriptHook();
		return 0;
	}
	if (IthCheckFile(L"Execle.exe"))
	{
		InsertTriangleHook();
		return 0;
	}
	if (IthCheckFile(L"pencil_production.mpg"))
	{
		InsertPensilHook();
		return 0;
	}
	if (IthFindFile(L"*.med"))
	{
		InsertMEDHook();
		return 0;
	}
	if (IthCheckFile(L"GameLib.dll"))
	{
		if (IthFindFile(L"dat\\*.dat"))
		{
			InsertAB2TryHook();
			return 0;
		}
	}
	return 1;
}
DWORD DetermineEngineByFile4()
{
	if (IthCheckFile(L"bmp.pak") && IthCheckFile(L"dsetup.dll"))
	{
		InsertDebonosuHook();
		return 0;
	}
	if (IthFindFile(L"C4.EXE") || IthFindFile(L"XEX.EXE"))
	{
		InsertC4Hook();
		return 0;
	}
	return 1;
}
DWORD DetermineEngineByProcessName()
{
	WCHAR str[MAX_PATH];
	wcscpy(str,process_name);
	_wcslwr(str);
	if (wcsstr(str,L"reallive"))
	{
		InsertRealliveHook();
		return 0;
	}
	if (wcsstr(str,L"siglusengine"))
	{
		InsertSiglusHook();
		return 0;
	}
	if (wcsstr(str,L"rugp"))
	{
		InsertRUGPHook();
		return 0;
	}
	if (wcsstr(str,L"igs_sample"))
	{
		InsertIronGameSystemHook();
		return 0;
	}
	DWORD len = wcslen(str);
	str[len - 3] = L'b';
	str[len - 2] = L'i';
	str[len - 1] = L'n';
	str[len] = 0;
	if (IthCheckFile(str))
	{
		InsertCaramelBoxHook();
		return 0;
	}
	return 1;
}
DWORD DetermineEngineOther()
{
	DWORD low,high;
	/*if (FillRange(L"mscorlib.ni.dll",&low,&high))
	{
		InsertDotNetHook1(low,high);
		return 0;
	}*/
	DWORD addr;
	if (GetFunctionAddr("SP_TextDraw",&addr,&low,&high,0))
	{
		if (addr)
		{
			InsertAliceHook1(addr,low,low+high);
			return 0;
		}
	}
	if (GetFunctionAddr("SP_SetTextSprite",&addr,&low,&high,0))
	{
		if (addr)
		{
			InsertAliceHook2(addr);
			return 0;
		}
	}
	
	if (IthGetFileInfo(L"*01",static_file_info))
	{	
		if (*(DWORD*)static_file_info==0)
		{
			static WCHAR static_search_name[MAX_PATH];	
			LPWSTR name=(LPWSTR)(static_file_info+0x5E);		
			int len=wcslen(name);		
			name[len-2]=L'*';	
			name[len-1]=0;		
			wcscpy(static_search_name,name);		
			IthGetFileInfo(static_search_name,static_file_info);		
			BYTE* ptr=static_file_info;		
			len=0;		
			while (*(DWORD*)ptr)		
			{	
				ptr+=*(DWORD*)ptr;		
				len++;			
			}	
			if (len>3)		
			{		
				InsertAbelHook();			
				return 0;			
			}		
		}	
	}
	return 1;
}
DWORD DetermineNoHookEngine()
{

	if (IthCheckFile(L"game_sys.exe"))
	{
		OutputConsole(L"Atelier Kaguya BY/TH");
		return 0;
	}
	if (IthFindFile(L"*.ykc"))
	{
		OutputConsole(L"HookSoft/Feng");
		return 0;
	}
	if (IthFindFile(L"*.bsa"))
	{
		OutputConsole(L"Bishop");
		return 0;
	}

	if (IthFindFile(L"*.pac"))
	{
		if (IthCheckFile(L"Thumbnail.pac"))
		{
			OutputConsole(L"GIGA");
			return 0;
		}
		if (SearchResourceString(L"SOFTPAL"))
		{
			OutputConsole(L"SoftPal UNiSONSHIFT");
			return 0;
		}
	}
	WCHAR str[MAX_PATH];
	DWORD i;
	for (i = 0; process_name[i]; i++)
	{
		
		str[i] = process_name[i];
		if (process_name[i] == L'.') break;
	}
	*(DWORD*)(str + i + 1) = 0x630068; //.hcb
	*(DWORD*)(str + i + 3) = 0x62;
	if (IthCheckFile(str))
	{
		OutputConsole(L"FVP");
		return 0;
	}
	return 1;
}
extern "C" DWORD __declspec(dllexport) DetermineEngineType()
{
	OutputConsole(L"Engine support module 2011.10.15");
	if (DetermineEngineByFile1()==0) return 0;
	if (DetermineEngineByFile2()==0) return 0;
	if (DetermineEngineByFile3()==0) return 0;
	if (DetermineEngineByFile4()==0) return 0;
	if (DetermineEngineByProcessName()==0) return 0;
	if (DetermineEngineOther()==0) return 0;
	if (DetermineNoHookEngine()==0)
	{
		OutputConsole(L"No special hook.");
		return 0;
	}
	OutputConsole(L"Unknown engine.");
	return 0;
}
static DWORD recv_esp, recv_eip;
static EXCEPTION_DISPOSITION ExceptHandler(EXCEPTION_RECORD *ExceptionRecord,
	void * EstablisherFrame, CONTEXT *ContextRecord, void * DispatcherContext )
{
	if (ExceptionRecord->ExceptionCode==0xC0000005)
	{

		module_limit=ExceptionRecord->ExceptionInformation[1];
		OutputDWORD(module_limit);
		__asm
		{
			mov eax,fs:[0x30]
			mov eax,[eax+0xC]
			mov eax,[eax+0xC]
			mov ecx,module_limit
			sub ecx,module_base
			mov [eax+0x20],ecx
		}
	}
	ContextRecord->Esp=recv_esp;
	ContextRecord->Eip=recv_eip;
	return ExceptionContinueExecution;
}
extern "C" DWORD __declspec(dllexport) IdentifyEngine()
{
	FillRange(process_name,&module_base,&module_limit);
	BYTE status=0;
	status=0;
	__asm
	{
		mov eax,seh_recover3
		mov recv_eip,eax
		push ExceptHandler
		push fs:[0]
		mov fs:[0],esp
		pushad
		mov recv_esp,esp
	}
	DetermineEngineType();status++;
	__asm
	{
seh_recover3:
		popad
		mov eax,[esp]
		mov fs:[0],eax
		add esp,8
	}
	if (status==0) OutputConsole(L"Fail to identify engine type.");		
	else OutputConsole(L"Initialized successfully.");
}
