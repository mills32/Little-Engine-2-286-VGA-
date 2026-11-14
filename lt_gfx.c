/*##########################################################################
	A lot of code from David Brackeen                                   
	http://www.brackeen.com/home/vga/                                     

	This is a 16-bit program.                     
	Remember to compile in the LARGE memory model!                        

	Please feel free to copy this source code.                            

	Used:	K1n9_Duk3's IMF Player - A simple IMF player for DOS
			Copyright (C) 2013-2016 K1n9_Duk3

			Based on the Apogee Sound System (ASS) and Wolfenstein 3-D (W3D)

			ASS is Copyright (C) 1994-1995 Apogee Software, Ltd.
			W3D is Copyright (C) 1992 Id Software, Inc.
	
##########################################################################*/

#include "lt__eng.h"

//Predefined structs
/*******************
OK let's do this console style, (of course You can do whatever you want).
At any moment there will only be:
You'll have to unload a music/map/tilset before loading another.
********************/
#define INPUT_STATUS_0		0x03da
#define SC_INDEX            0x03c4    // VGA sequence controller
#define SC_DATA             0x03c5

#define GC_INDEX            0x03ce    // VGA graphics controller
#define GC_DATA             0x03cf

#define MAP_MASK            0x02
#define ALL_PLANES          0xff02
 

extern byte LT_DETECTED_CARD;
word LT_VRAM_Logical_Width;
// One map in ram stored at "LT_map"
word LT_map_width;
word LT_map_height;
word LT_map_ntiles;
word far *LT_map_data;
byte far *LT_map_collision;
// One tileset 
word LT_tileset_width;
word LT_tileset_height;
word LT_tileset_ntiles;
byte LT_tileset_palette[256*3];
byte far *LT_tileset_data;
extern unsigned char *LT_sprite_data; // 
extern SPRITE far *sprite;
extern byte far *sprite_id_table;
byte LT_Loaded_Image = 0; //If you load a 320x240 image (to the second page), the delete loading interrupt will paste it to first page
byte LT_PAL_TIMER = 0;
byte LT_EGA_FADE_STATE = 0;
byte LT_TEXT_TRANSLUCENT = 0;
word LT_FrameSkip = 0;
byte *LT_Filename; //Contains name of the images oppened, for LT_Load_BMP function 

void LT_Error(char *error, char *file);
void LT_Reset_Sprite_Stack();

extern byte LT_MUSIC_LOADED;
extern word LT_Sprite_Size_Table_EGA_32[];
extern byte LT_SFX_MODE;
extern byte LT_Sprite_Stack;
extern byte LT_Sprite_Stack_Table[];
extern byte LT_Active_AI_Sprites[];
extern byte selected_AI_sprite;
extern void (*LT_Draw_Sprites)(void);
extern byte LT_SPRITES_PLANE_MODE;
void LT_Draw_Sprites_1();
void LT_Draw_Sprites_2();

//GLOBAL VARIABLES

//Frames for stored 128x128 animations, only on IMAGE mode (not map mode).
//Frames stored between the two framebuffers which can't scroll more than +32/-32 X Y.
word LT2_Anim128_VRAM[12] = {
	0x3B60,0x4360,0x4B60,0x5360,0x5B60,0x6360,
	0xABE0,0xB3E0,0xBBE0,0xC3E0,0xCBE0,0xD3E0,
};

//Max 6 slots of two 128x128 frame animations
//Can also use one slot of 12 128x128 frames
byte LT2_Anim128_Frame[6] = {0,0,0,0,0,0};
byte LT2_Anim128_Speed[6] = {0,0,0,0,0,0};

word FONT_VRAM = 0;
word TILE_VRAM = 0;
//Palette for fading
byte LT_Temp_palette[32*3];

//Palette cycling
byte LT_Cycle_paldata[32*3]; //2 palettes of 16 colors each (duplicated to simplify code) for cycling colors.
byte LT_Cycle_palframe = 0;
byte LT_Cycle_palcounter = 0;

//This points to video memory.
byte *VGA=(byte *)0xA0000000L; 

//Map Scrolling
byte scr_delay = 0; //copy tile data in several frames to avoid slowdown
int SCR_X = 0;
int SCR_Y = 0;
int TGA_SCR_X = 0;
int TGA_SCR_Y = 0;
int TANDY_SCROLL_X = 0;
int TANDY_SCROLL_Y = 0;
int LT_C64 = 0;
int LT_current_x = 0;
int LT_last_x = 0;
int LT_current_y = 0;
int LT_last_y = 0;
int LT_map_offset = 0;
int LT_map_offset_y = 0;
int LT_map_offset_Endless = 0;
int LT_follow = 0;
byte LT_var = 0;//To disable fade in/out
int vpage = 0;
int Enemies = 0;

word LT_offset_vram_TXT;

extern void interrupt (*LT_old_time_handler)(void); 
void interrupt (far * LT_getvect(byte intr))();
void LT_setvect(byte intr, void interrupt (far *func)());

byte tga_page = 0;

void DAT_Seek2(word fp,char *dat_string); 
void LT_fread(word file_handle,word bytes,void *buffer);
unsigned long LT_fseek(word file_handle,unsigned long bytes,byte origin);
extern int LT_SPRITE_PLAYER;
void /*interrupt*/ PC_Speaker_SFX_Player(void);

word _strlen(char *str){
	word len = 0;
	while(1){if (str[len] == 0) break;len++;}
	return len;
}

void _memcpy(void *dest, void *src, word number){
	asm push ds;asm push si;asm push es;asm push di;
	
	asm lds	si,src
	asm les	di,dest
	asm mov cx,number
	asm rep movsb //ds:si to es:di
	
	asm pop di;asm pop es;asm pop si;asm pop ds;
}


void Check_Graphics_Card(){
	byte reg = 0;
	asm mov ax,0x1A00
	asm mov bl,0x32
	asm int 0x10; asm mov reg,al;
	if (reg == 0x1A) {_printf("Card detected: VGA\n\r$"); LT_DETECTED_CARD = 1;}
	else {
		asm mov ah,0x12
		asm mov bx,0x1010
		asm int 0x10; asm mov reg,bh;
		if (reg == 0) {
			_printf("Card detected: EGA\n\r$");
			LT_DETECTED_CARD = 0;
		} else {
			asm mov ah,0x0F
			asm mov bl,0x00
			asm int 0x10; asm mov reg,al;
			if (reg == 0x07) {
				_printf("Card detected: Hercules\n\r$");
				LT_Exit();
			} else {
				byte mode = 0;
				//CARD is a CGA or TANDY, set TANDY mode 9 to check it it is a tandy
				asm mov ax,0x0009;asm int 0x10; //MODE 9 (TANDY 320x200 16 color)
				asm mov ax,0x0F00;asm int 0x10; //Check mode: AL = video mode
				asm mov mode,al
				if (mode == 9) {
					asm mov ax,0x0003;asm int 0x10; //GO BACK TO TEXT MODE
					_printf("Card detected: TANDY\n\r$");
					LT_DETECTED_CARD = 2;
				} else {
					_printf("Card detected: CGA\n\r$");
					LT_DETECTED_CARD = 3;
				}
			}
		}
	}
	
	Clearkb();
}


void LT_VGA_Enable_4Planes(){
	word planes = 0x0F02;
	if (LT_TEXT_TRANSLUCENT) planes = 0x0702;
	//read mode does not matter for this
	asm mov dx,03c4h  	//dx = indexregister
	asm mov ax,planes	//INDEX = MASK MAP, 3d4
	asm out dx,ax 		//write all the bitplanes.
	asm mov dx,03ceh 	//dx = indexregister 3ce
	asm mov ax,0008h		
	asm out dx,ax
}

void LT_VGA_Return_4Planes(){
	asm mov dx,03ceh +1 //dx = indexregister
	asm mov ax,00ffh		
	asm out dx,ax
	if (LT_TEXT_TRANSLUCENT){
		asm mov dx,03C4h	//dx = indexregister
		asm mov ax,0F02h	//INDEX = MASK MAP, 
		asm out dx,ax 		//write plane 1.
	}
}

void LT_vsync(){
	//Try to catch Vertical retrace
	asm mov	dx,0x3DA; asm mov bl,0x08;
	WaitNotVsync: asm in al,dx; asm test al,bl; asm jnz WaitNotVsync;
	WaitVsync:    asm in al,dx; asm test al,bl; asm jz WaitVsync;
	//while( inp( INPUT_STATUS_0 ) & 0x08 );
	//while( !(inp( INPUT_STATUS_0 ) & 0x08 ) );
}

void LT_ClearScreen(){
	VGA_ClearPalette();
	asm mov dx,0x03CE
	asm mov ax,0x0005	//write mode 0
	asm out dx,ax
	asm mov	dx,0x03CE	  //Set mask register
	asm mov ax,0xFF08
	asm out	dx,ax
	LT_memset(VGA,0x00,(352>>2)*240);
}

//So I had to add a "double buffer" just to update the sprites without flicker
//Don't worry!! I only update 19 tiles and the sprites every frame, so it is even faster than before on 8088 and 8086.
//The bad news is, I had to reduce map size and tilecount, but it is stil cool!
//These values are added to scroll registers so it iterates between the "two pages"
word pageflip[] = {0,576};
word gfx_draw_page = 1;

byte LT_mode_sprite = 0;
byte LT_pixel_format = 0;


extern unsigned char *LT_sprite_data;


byte LT_Interrupt_Sound[16] = {30,31,32,33,34,35,30,20,0,30,0,4,20,0,0,0};


void LT_Start_Loading(){
	LT_Cycle_palcounter = 0;
	LT_Stop_Music();
	LT_Fade_out();
	LT_Disable_Speaker();
	LT_Reset_Sprite_Stack();
	LT_Unload_Sprites();
	LT_Reset_Palette_Cycle();
	LT_MUSIC_LOADED = 0;
	//Wait Vsync
	LT_vsync();

	//Reset scroll
	asm mov dx,0x03D4;
	asm mov ax,0x000D; asm out dx,ax;
	asm mov ax,0x000C; asm out dx,ax;
	asm mov dx,0x03C0;
	asm mov al,0x33; asm out dx,al;	//0x20 | 0x13 (palette normal operation | Pel panning reg)
	asm mov al,0x00; asm out dx,al;	//Panning = 0
	
	//reset map mask just in case
	asm mov dx,0x03C4; asm mov ax,0x0F02; asm out dx,ax;
	LT_SPRITES_PLANE_MODE = 0x0F;

	SCR_X = 0; SCR_Y = 0;
	
	LT_ClearScreen();
	
	LT_memset(sprite_id_table,0,36*512);
	
	//Wait Vsync
	LT_vsync();
	LT_Keys[LT_ESC] = 0;LT_Keys[LT_ENTER] = 0;LT_Keys[LT_ACTION] = 0; LT_Keys[LT_JUMP] = 0;
}

void LT_End_Loading(){
	//unsigned long spd = 1193182/60;
	LT_Keys[LT_ESC] = 0;LT_Keys[LT_ENTER] = 0;LT_Keys[LT_ACTION] = 0; LT_Keys[LT_JUMP] = 0;

	if (LT_Loaded_Image){
		int i;
		LT_VGA_Enable_4Planes();
		for (i = 0; i < LT_VRAM_Logical_Width*240;i++) VGA[i] = VGA[i + (pageflip[1]*LT_VRAM_Logical_Width)];
		LT_Draw_Sprites = LT_Draw_Sprites_1;
		LT_Loaded_Image = 0;
		LT_Fade_in();
	} else {
		LT_Draw_Sprites = LT_Draw_Sprites_2;
	}
	LT_VGA_Return_4Planes();
}



byte p[4] = {0,2,4,6};
byte p1[8] = {0,1,2,3,4,5,6,7};
byte pix;

void PC_Speaker_SFX_Player();//I don't want to use interrupts, play pc speaker sfx here if playing == 1

//////////////////////
//Hardware scrolling// 
//////////////////////
void (*LT_WaitVsync)();

//This is smooth in dosbox, PCem, 86Box, original EGA/VGA many VGA compatibles on very slow systems.
void LT_WaitVsync_VGA(){
	word x = SCR_X;
	word y = SCR_Y + pageflip[gfx_draw_page&1];

	y*=LT_VRAM_Logical_Width;
	y += x>>3; pix = p1[SCR_X & 7];
	gfx_draw_page++;
	
	//change scroll registers: 
	asm {
		cli
		mov	cl,8; mov dx,003d4h; //VGA PORT
		mov ax,y; shl ax,cl; or ax,00Dh; out dx,ax; //(y << 8) | 0x0D; to VGA port
		mov ax,y; and ax,0FF00h; or ax,00Ch; out dx,ax;	//(y & 0xFF00) | 0x0C; to VGA port
		sti
	}
	//Try to catch Vertical retrace
	asm mov	dx,0x3DA; asm mov bl,0x08;
	WaitNotVsync: asm in al,dx; asm test al,bl; asm jnz WaitNotVsync;
	WaitVsync:    asm in al,dx; asm test al,bl; asm jz WaitVsync;
	//The smooth panning happens here
	asm {
		cli
		mov dx,0x03C0;
		mov al,0x33; out dx,al;	//0x20 | 0x13 (palette normal operation | Pel panning reg)	
		mov al,byte ptr pix; out dx,al;
		sti
	}
	
	//I don't think this is necessary
	//asm mov		dx,INPUT_STATUS_0 //Read input status, to Reset the VGA flip/flop
	//_ac = inp(AC_INDEX);//Store the value of the controller
	//AC index 0x03c0
	
	//Restore controller value
	//asm mov ax,word ptr _ac
	//asm out dx,ax
	//enable interrupts
}

void LT_WaitVsync_VGA_Debug(){
	word x = SCR_X;
	word y = SCR_Y + pageflip[gfx_draw_page&1];

	y*=LT_VRAM_Logical_Width;
	y += x>>3; pix = p1[SCR_X & 7];
	gfx_draw_page++;

	//change scroll registers:
	asm {
		cli
		mov	cl,8; mov dx,003d4h; //VGA PORT
		mov ax,y; shl ax,cl; or ax,00Dh; out dx,ax; //(y << 8) | 0x0D; to VGA port
		mov ax,y; and ax,0FF00h; or ax,00Ch; out dx,ax;	//(y & 0xFF00) | 0x0C; to VGA port
		sti
	}
	//debug
	asm mov dx,0x03C8; asm mov al,0; asm out dx,al;
	asm inc dx;
	asm mov al,0x00; asm out dx,al; asm out dx,al; asm out dx,al;
	//Try to catch Vertical retrace
	asm mov	dx,0x3DA; asm mov bl,0x08;
	WaitNotVsync: asm in al,dx; asm test al,bl; asm jnz WaitNotVsync;
	WaitVsync:    asm in al,dx; asm test al,bl; asm jz WaitVsync;
	//debug
	asm mov dx,0x03C8; asm mov al,0; asm out dx,al;
	asm inc dx;
	asm mov al,0xFF; asm out dx,al; asm xor al,al; asm out dx,al; asm out dx,al;

	//The smooth panning happens here
	asm {
		cli
		mov dx,0x03C0;
		mov al,0x33; out dx,al;	//0x20 | 0x13 (palette normal operation | Pel panning reg)
		mov al,byte ptr pix; out dx,al;
		sti
	}

	//I don't think this is necessary
	//asm mov		dx,INPUT_STATUS_0 //Read input status, to Reset the VGA flip/flop
	//_ac = inp(AC_INDEX);//Store the value of the controller
	//AC index 0x03c0

	//Restore controller value
	//asm mov ax,word ptr _ac
	//asm out dx,ax
	//enable interrupts
}

//Alternative functions taken from keen70Hz, should be compatible with most cards
static unsigned char MaxHblankLength = 1;
static unsigned char benchmark_hblank_length(){
	unsigned char hblank_length;
	asm cli
	asm mov dx, 3DAh

wait_blank_end:  // if we are in hblank or vblank, wait for it to end
	asm in al, dx  // Read 3DAh - Status Register
	asm test al, 1 // Bit 0: Display Blank
	asm jnz wait_blank_end

wait_active_end: // wait for the end of the active scanline
	asm in al, dx  // Read 3DAh again
	asm test al, 1
	asm jz wait_active_end

	// We are now right at the start of a blank period, either hblank or vblank
	// Calculate how many cycles this blank lasts.
	asm mov cx, 0    // store in cl: seen_status, ch: hblank_len
calc_blank_length: // measure how many I/O port read cmds we can do in blank
	asm in al, dx    // Read port.
	asm or cl, al    // Track in cl if this blank period contained vsync bit
	asm inc ch       // Accumulate count of I/Os performed
	asm test al, 1   // Still in blank period?
	asm jnz calc_blank_length

	asm test cl, 8   // Blank period is now over. Check if it included vsync
	asm jnz wait_blank_end // If so, restart all from scratch. We wanted hblank.
  asm mov hblank_length, ch
  asm sti
  return hblank_length;
}

void CalibrateMaxHblankLength(){
	int i;
	MaxHblankLength = 1;
	// Benchmark: What is the maximum amount of ISA/PCI/AGP port I/Os that can be
	// performed within a single hblank period?
	// I.e. find what is the max length on the bus that a Hblank can take.
	for(i = 0; i < 1000; ++i)
	{
		int length = benchmark_hblank_length();
		if (length > MaxHblankLength) MaxHblankLength = length;
	}
	// Add just a tiny bit more over the count that we got.
	if (MaxHblankLength <= 253) MaxHblankLength += 2;
	//printf("MaxHblankLength: %d\n", (int)MaxHblankLength);
}

void LT_WaitVsync_VGA_Compatible(){
	word x = SCR_X;
	word y = SCR_Y + pageflip[gfx_draw_page&1];
	int i;
	
	//
	y*=LT_VRAM_Logical_Width;
	y += x>>3; pix = p1[SCR_X & 7]; //EGA
	gfx_draw_page++;

	asm mov dx, 3DAh

	wait_for_active_picture:// Wait until we are in visible picture area.
	asm in al,dx			// Read 3DAh - Status Register
	asm test al,1			// Bit 0: set if we are in Display Blank.
	asm jnz wait_for_active_picture

	// We are now in visible picture area (so can't be in vsync, or right headed
	// into it)
	asm mov cx,0
	wait_for_hblank:
		asm cli // Enter time critical stage: estimating the length of a blank.
		asm mov cl, MaxHblankLength // Reset wait counter to default

	loop_hblank_length_times:
		asm in al,dx	// Read status port
		asm test al,1	// Are we in display blank?
		asm jnz in_blank// If 1, then we are in blank
		// Else 0, we are still in visible picture area. Enable interrupts and restart the wait.
		asm sti			
		asm jmp wait_for_hblank 

		in_blank:		// We are in blank, but have we slipped over to vsync?
		asm test al,8	// Test if in vsync?
		asm jz in_blank_not_vsync // If 0, we are not in vsync

		asm sti			// Else 1, we are in vsync, so we blew it. Restart the wait.
		asm jmp wait_for_active_picture

		in_blank_not_vsync: // We are in blank, either hblank or vblank
		// Decrement search counter
		asm loop loop_hblank_length_times // And loop back if we still need to.

	// If we get here, we have entered a display blank period that is longer than
	// a hblank interval, so we conclude we must have just now entered a vblank.
	// (but we aren't yet at start of vsync)
	// Interrupts are disabled at this point, so we can safely update
	// Display Start (DS) and Horizontal Shift Count (HS) registers so all
	// adapters will latch it properly, with all their varying quirky behaviors.
	// (Pedantically, it is tiny bit better better to write DS register before HS,
	// because IBM EGA and VGA latch the DS register before the HS register)

	//change scroll registers: 
	asm mov dx,003d4h //VGA PORT
	asm mov	cl,8
	asm mov ax,y
	asm	shl ax,cl
	asm	or	ax,00Dh	//LOW_ADDRESS 0x0D
	asm out dx,ax	//(y << 8) | 0x0D to VGA port
	asm mov ax,y
	asm	and ax,0FF00h
	asm	or	ax,00Ch	//HIGH_ADDRESS 0x0C;
	asm out dx,ax	//(y & 0xFF00) | 0x0C to VGA port
	
	//AC index 0x03c0
	asm mov dx,0x03C0
	asm mov al,0x33		//0x20 | 0x13 (palette normal operation | Pel panning reg)	
	asm out dx,al
	asm mov al,byte ptr pix
	asm out dx,al

	//enable interrupts
	asm sti
}


//Generic function that may work on many "SVGA" cards on very fast systems
//Assumes a very fast PC which will catch the vsync at the very beginning,
//and the CPU will update all registers at once, no need to optimize in asm.
//Tested on toshiba satellite laptop
void LT_WaitVsync_VGA_crappy(){//77.128
	word x = SCR_X;
	word y = SCR_Y + pageflip[gfx_draw_page&1];
	int i;
	
	y*=LT_VRAM_Logical_Width;
	y += x>>3; pix = p1[SCR_X & 7]; //EGA
	gfx_draw_page++;

	//Try to catch Vertical retrace
	asm mov	dx,0x3DA; asm mov bl,0x08;
	WaitNotVsync: asm in al,dx; asm test al,bl; asm jnz WaitNotVsync;
	WaitVsync:    asm in al,dx; asm test al,bl; asm jz WaitVsync;
	
	//change scroll registers: 
	asm {
		cli
		mov	cl,8; mov dx,003d4h; //VGA PORT
		mov bx,y; 
		mov al,0x0C; mov ah,bh; out dx,ax;	//(y & 0xFF00) | 0x0C; to VGA port	
		mov al,0x0D; mov ah,bl; out dx,ax;  //(y << 8) | 0x0D; to VGA port
		
		mov dx,0x03C0
		mov al,0x33		//0x20 | 0x13 (palette normal operation | Pel panning reg)	
		out dx,al
		mov al,byte ptr pix
		out dx,al
		sti
	}	
}


byte panning = 0;
byte static_screen = 0;


//Updates the graphics and sprites
void LT_Update(int sprite_follow, int sprite){
	LT_Update_AI_Sprites();
	LT_Draw_Sprites();
	if (sprite_follow) LT_scroll_follow(sprite);
	if (!LT_IMAGE_MODE) {
		LT_Scroll_Map();
	}
	PC_Speaker_SFX_Player();
	LT_WaitVsync();
	LT_Read_Joystick();
}


void LT_Wait(byte seconds){
	seconds*=60;
	while (seconds){seconds--;LT_Update(0,0);}
}

// load_8x8 fonts to VRAM (64 characters)
/*void LT_Load_Font(char *file, char *dat_string, int c0, int c1, int c2, int c3){

}*/

//Print 8x8 tiles, not very optimized, do not use on game loop, just to print stuff once
void LT_Print(word x, word y, word w, char *string){
	byte *FONT_ADDRESS = LT2_FONT;
	byte datastring;
	word size = _strlen(string);
	word i = 0;
	word line = 0;
	word screen_offset;
	word lwidth = LT_VRAM_Logical_Width-1;
	word lwidth2 = LT_VRAM_Logical_Width*7;
	word line_jump = (LT_VRAM_Logical_Width*8) -w;
	//if (LT_TEXT_TRANSLUCENT) planes = 0x0702;
	y = (y<<3);
	screen_offset = (y*50);//if (size > 40) size = 40;
	asm{
		push ds; push di; push si
		mov	di,screen_offset; mov ax,x; add di,ax;
	}
	printloop3:
	asm{
		les  bx,string; add bx,i; mov  al,byte ptr es:[bx]; //AL = character
		xor  ah,ah
		lds		si,FONT_ADDRESS;			//ds:si FONT TILE ADDRESS
		shl		ax,3; add	si,ax;			//go to desired character
		mov 	ax,0A000h; mov 	es,ax		//es:di destination address
		mov	bx,lwidth
		//UNWRAPPED COPY 8x8 TILE LOOP
		mov al,byte ptr es:[di]; movsb; add di,bx;
		mov al,byte ptr es:[di]; movsb; add di,bx;
		mov al,byte ptr es:[di]; movsb; add di,bx;
		mov al,byte ptr es:[di]; movsb; add di,bx;
		mov al,byte ptr es:[di]; movsb; add di,bx;
		mov al,byte ptr es:[di]; movsb; add di,bx;
		mov al,byte ptr es:[di]; movsb; add di,bx;
		mov al,byte ptr es:[di]; movsb;
		//END LOOP
		
		sub	di,lwidth2
		
		inc		line
	    mov		ax,line
	    cmp		ax,w
	    jne		no_jump_line
		
		add		di,line_jump
		mov 	line,0
	}
	no_jump_line:
	i++;
	asm{
		dec		size
		jnz		printloop3
		
		//END
		
		pop si
		pop di
		pop ds
	}
}

//Print a three digit variable on the window
void LT_Print_Variable(byte x, byte y, word var){
	byte *FONT_ADDRESS = LT2_FONT;
	word screen_offset = (50<<2) + (x) + 3;
	asm{
		push ds
		push di
		push si

		mov dx,0x03C4; mov ax,0x0F02; out dx,ax   // map mask: all 4 planes
		mov dx,0x03CE;
		mov ax,0x0005; out dx,ax                  // write mode 0, read mode 0
		mov ax,0x0001; out dx,ax                  // Enable Set/Reset = 0 (CPU data goes to the planes)
		mov ax,0xFF08; out dx,ax			// bit mask = FF (all 8 pixels replaced)

		//DIVIDE VAR BY 10, GET REMAINDER, DRAW DIGIT
		mov dx,0 
		mov ax,0
		mov	ax,[var]				
		mov bx,10
		div bx								//Divides var by 10 // dx = remainder // ax = var/10
		mov [var],ax
		
		lds		si, FONT_ADDRESS;			//ds:si VRAM FONT TILE ADDRESS = 586*(336/4);
		//go to desired tile
		add		dx,16
		shl		dx,3
		add		si,dx
		
		mov		di,screen_offset			//es:di destination address							
		mov 	ax,0A000h
		mov 	es,ax
		mov		ax,49
		movsb; add di,ax
		movsb; add di,ax
		movsb; add di,ax
		movsb; add di,ax
		movsb; add di,ax
		movsb; add di,ax
		movsb; add di,ax
		movsb;
	}
	asm{//DIVIDE VAR BY 10, GET REMAINDER, DRAW DIGIT
		mov dx,0 
		mov ax,0
		mov	ax,[var]				
		mov bx,10
		div bx								//Divides var by 10 // dx = remainder // ax = var/10
		mov [var],ax
		
		lds	si,FONT_ADDRESS;			//ds:si VRAM FONT TILE ADDRESS = 586*(336/4);
		
		//go to desired tile
		add		dx,16
		shl		dx,3
		add		si,dx
		
		mov		di,screen_offset			//es:di destination address	
		sub		di,1
		mov 	ax,0A000h
		mov 	es,ax
		mov		ax,49
		movsb; add di,ax;
		movsb; add di,ax;
		movsb; add di,ax;
		movsb; add di,ax;
		movsb; add di,ax;
		movsb; add di,ax;
		movsb; add di,ax;
		movsb;

	}
	asm{//DIVIDE VAR BY 10, GET REMAINDER, DRAW DIGIT
		mov dx,0 
		mov ax,0
		mov	ax,[var]				
		mov bx,10
		div bx								//Divides var by 10 // dx = remainder // ax = var/10
		mov [var],ax
		
		lds		si,FONT_ADDRESS;			//ds:si VRAM FONT TILE ADDRESS = 586*(336/4);
		
		//go to desired tile
		add		dx,16
		shl		dx,3
		add		si,dx
		
		mov		di,screen_offset			//es:di destination address	
		sub		di,2
		mov 	ax,0A000h
		mov 	es,ax
		mov ax,49
		movsb; add di,ax;
		movsb; add di,ax;
		movsb; add di,ax;
		movsb; add di,ax;
		movsb; add di,ax;
		movsb; add di,ax;
		movsb; add di,ax;
		movsb;
	}

	asm{//END
		pop si
		pop di
		pop ds
	}
}


int LT_deleting_text = 0;
int LT_Text_Speak = 0;
int LT_Text_Speak_Pos = 0;
int LT_Text_Speak_End = 0;
int LT_Text_y2; //page 0 text
int LT_Text_y3;	//page 1 text
int LT_Text_size;
int LT_Text_i = 0; int LT_Text_j = 0;
byte LT_Speak_Sound[16] = {30,31,32,33,34,35,30,20,0,30,0,4,20,0,0,0};
byte LT_Speak_Sound_AdLib[11] = {0x62,0x04,0x04,0x1E,0xF7,0xB1,0xF0,0xF0,0x00,0x00,0x03};
unsigned char up[40]; unsigned char mid[40]; unsigned char down[40];
void draw_text_box(word x, word y, byte w, byte h, byte mode, char *string){
	int i;
	int y1;
	if (x+w > 40) x = 40-w;
	if (y+h > 30) y = 30-h;

	x += (SCR_X>>3);
	y += (SCR_Y>>3);

	y1 = y+72;	//page 1 box
	if (!LT_Text_Speak){//
		int size = 0;
		if (_strlen(string) > (w*h)) size = w*h;
		else size = _strlen(string);
		LT_Text_size = size;
		LT_Text_y2 = y+1;
		LT_Text_y3 = y+1+72;
		//BOX characters 8-15, with masks for box shapes at 24-31
		up[0] = 8; up[w+1] = 9; up[w+2] = 0;
		mid[0] = 14; mid[w+1] = 15; mid[w+2] = 0;
		down[0] = 10; down[w+1] = 11; down[w+2] = 0;
		for (i = 1; i<w+1; i++){up[i] = 12; mid[i] = ' ';down[i] = 13;}

		//Set to draw with transparency
		outport(0x03C4,0x0F02);
		outport(0x03CE,0x0007); outport(0x03CE,0x0B05);
		outport(0x03CE,0x0F00);

		//Draw Box
		//Page 0
		LT_Print(x,y,w+2,up);y++;
		for (i = 0; i<h; i++) {LT_Print(x,y,w+2,mid);y++;}
		LT_Print(x,y,w+2,down);
		//Page 1
		if (!LT_Loaded_Image){
			LT_Print(x,y1,w+2,up);y1++;
			for (i = 0; i<h; i++) {LT_Print(x,y1,w+2,mid);y1++;}
			LT_Print(x,y1,w+2,down);
		}

		LT_Text_Speak_Pos = 0;
		LT_Text_i = 0; LT_Text_j = 0;
	}

	//Set to draw black and white
	outport(0x03C4,0x0F02); outport(0x03CE,0x0000);
	outport(0x03CE,0x0001); outport(0x03CE,0x0005);
	outport(0x03CE,0xFF08);

	//Print text inside box
	if (!mode){
		LT_Print(x+1,LT_Text_y2,w,string);
		if (!LT_Loaded_Image) LT_Print(x+1,LT_Text_y3,w,string);
	} else { //Speaking mode, draw characters one by one
		LT_Text_Speak = 1;
		up[0] = string[LT_Text_Speak_Pos];
		up[1] = 0;
		if (up[0] == ' '){
			if (LT_SFX_MODE) LT_Play_AdLib_SFX(LT_Speak_Sound_AdLib,8,2,2);
			else LT_Play_PC_Speaker_SFX(LT_Speak_Sound);
		}
		LT_Print(x+1+LT_Text_i,LT_Text_y2,2,up);
		LT_Print(x+1+LT_Text_i,LT_Text_y3,2,up);
		//Update
		if (LT_Text_i < w-1) {LT_Text_i++;LT_Text_Speak_Pos++;}
		else {
			
			LT_Text_Speak_Pos++;
			LT_Text_i = 0; 
			if (LT_Text_j < h) {LT_Text_j++; LT_Text_y2++;LT_Text_y3++;}
		}
		if (LT_Text_Speak_Pos == LT_Text_size){
			LT_Text_Speak_Pos = 0;
			LT_Text_Speak = 0;
			LT_Text_Speak_End = 1;
		}
	}
}

void delete_text_box(word x, word y, byte w, byte h){
	byte ntile = 0;
	word xx; 
	word yy;
	byte oddw = 0;
	byte oddh = 0;
	byte tilesize;
	tilesize = 5;
	x += (SCR_X>>3);
	y += (SCR_Y>>3);
	x = x>>1; y = y>>1;
	if (w&1) oddw = 1;
	if (h&1) oddh = 1;
	w = (w>>1) + 2 + oddw;
	h = (h>>1) + 2 + oddh;
	LT_deleting_text = 1;
	//Draw map tiles
	for (yy = y;yy<y+h;yy++){
		for (xx = x;xx<x+w;xx++){
			ntile = (LT_map_data[(yy<<9)+   xx] - TILE_VRAM) >> tilesize;
			LT_Edit_MapTile(xx,yy,ntile,0);
		}
	}
	LT_deleting_text = 0;
}

//LT_Draw_Text_Box: prints text inside a box
//-mode 0: simple text box
//-mode 1: simple "talking" text box (writes characters 1 by 1)
//-mode 2: info box no talking, waits for "key1" to close (restores bkg map)	   
//-mode 3: info box talking mode.
//-mode 4: info box no talking, two options key1,key2 (Y/N continue/exit...)
//-mode 5: info box talking, two options.
//-mode 6: manualy delete a mode 0 or 1 box.

// keys = scancodes.
//returns 1 if mode = 4 or mode = 5 and key2 pressed. else 0 
//if bkg consist of an image, this can't restore it.

byte LT_Draw_Text_Box(word x, word y, byte w, byte h, word mode, byte key1, byte key2, char* text){
	int loop = 1;
	byte val = 0;

	if (mode > 6) mode = 6;
	if (mode != 6){
	if (mode > 1) while (LT_Keys[key1]) LT_Read_Joystick();
	if (mode&1){//Talking
		LT_Text_Speak_End = 0;
		while (!LT_Text_Speak_End){
			draw_text_box(x,y,w,h,1,text);
			PC_Speaker_SFX_Player();
			LT_Play_Music();
			LT_Vsync();
		}
		LT_Text_Speak_End = 0;
	} else draw_text_box(x,y,w,h,0,text);
	if (mode > 1){
		asm in al, 61h; asm and al, 252; asm out 61h, al //Disable speaker
		while (loop){
			PC_Speaker_SFX_Player();
			LT_Play_Music();
			LT_Read_Joystick();
			if (LT_Keys[key1]) {loop = 0; val = 0;}
			if ((LT_Keys[key2]) && (mode > 3)) { loop = 0; val = 1;}
			LT_vsync();
		}
		while (LT_Keys[key1]) {LT_Play_Music();LT_Read_Joystick();LT_vsync();}
	}
	}
	if (mode > 1) delete_text_Box(x,y,w,h);
	LT_VGA_Return_4Planes();
	return val;
}

//Load and paste 320x200 image for complex images.
extern byte loading_logo;
void LT_Load_Image(char *file,char* dat_string){
	word VGA_index = 0;
	word f;
	int y;
	int plane;
	unsigned char planemask = 0x01;
	unsigned char rowbuf[40];
	unsigned char type = 0;
	unsigned short width = 0;
	unsigned short height = 0;

	LT_Filename = file;
	f = LT_fopen(file,0);
	if(!f) LT_Error("Can't find$",file);
	DAT_Seek2(f,dat_string);

	LT_fread(f,1,&type);
	LT_fread(f,2,&width);
	LT_fread(f,2,&height);

	if(type != 17) LT_Error("Not image  $",file);
	if(width != 320) LT_Error("Not image  $",file);
	if(height != 240) LT_Error("Not image  $",file);

	LT_fread(f,3*24,LT_tileset_palette);
	asm cli;
	//Read image to vram
	for (plane = 0;plane < 4; plane++){
		VGA_index = pageflip[1]*50; //Second page
		for (y = 0;y < 240; y++){
			//fread interferes with writte settings
			LT_fread(f,40,rowbuf);
			//reset every line copied, it is not slow at all, and we save a lot of ram
			asm {
			mov dx,0x03CE;
			mov ax,0x0001; out dx,ax;
			mov ax,0xFF08; out dx,ax;
			mov ax,0x0005; out dx,ax;
			mov dx,0x03C4;
			mov ah,planemask; mov al,0x02; out dx,ax;
			}
			_memcpy(&VGA[VGA_index],rowbuf,40);
			VGA_index+=50;
		}
		planemask <<= 1;
	}
	asm sti;

	LT_fclose(f);

	loading_logo = 0;
	LT_Loaded_Image = 1;
}

// load_128x128 frames to VRAM
void LT_Load_Animation(char *file,char* dat_string){
	word VGA_index = 0;
	word f;
	int x,y;
	int plane;
	unsigned char planemask = 0x01;
	unsigned char rowbuf[128];
	unsigned char type = 0;
	unsigned short width = 0;
	unsigned short height = 0;

	LT_Filename = file;
	f = LT_fopen(file,0);
	if(!f) LT_Error("Can't find$",file);
	DAT_Seek2(f,dat_string);

	LT_fread(f,1,&type);
	LT_fread(f,2,&width);
	LT_fread(f,2,&height);

	if(type != 20) LT_Error("Not animation $",file);
	if(width != 512) LT_Error("Not animation  $",file);
	if(height != 384) LT_Error("Not animation $",file);

	asm cli;
	//Read image to vram
	for (plane = 0;plane < 4; plane++){
		for (y = 0;y < 12; y++){
			VGA_index = LT2_Anim128_VRAM[y];
			for (x = 0;x < 16; x++){//128 lines
				//fread interferes with writte settings
				LT_fread(f,128,rowbuf);//16x8 (8 lines)
				//reset every copy, it is not slow at all, and we save a lot of ram
				asm {
					mov dx,0x03CE;
					mov ax,0x0001; out dx,ax;
					mov ax,0xFF08; out dx,ax;
					mov ax,0x0005; out dx,ax;
					mov dx,0x03C4;
					mov ah,planemask; mov al,0x02; out dx,ax;
				}
				_memcpy(&VGA[VGA_index],rowbuf,128);
				VGA_index+=128;
			}
		}
		planemask <<= 1;
	}
	asm sti;

	LT_fclose(f);
}


void LT_Draw_Animation(int slot, unsigned short x, unsigned short y, unsigned char *animation, int speed){
	byte nframes = 0; byte frame = 0;
	word anim_data;
	word screen_offset = ((y+pageflip[gfx_draw_page&1])*50)+(x>>3);

	nframes = animation[0];
	frame = animation[1+LT2_Anim128_Frame[slot]];
	if (frame > 11) frame = 0;
	anim_data = LT2_Anim128_VRAM[frame];

	LT2_Anim128_Speed[slot]++;
	if (LT2_Anim128_Speed[slot] == speed) {
		LT2_Anim128_Speed[slot] = 0;
		LT2_Anim128_Frame[slot]++;
		if (LT2_Anim128_Frame[slot] > nframes) LT2_Anim128_Frame[slot] = 0;
	}

	asm{
		push ds;push di;push si;

		mov dx,0x03C4; mov ax,0x0F02; out dx,ax;
		mov dx,0x03CE;
		mov ax,0x0205; out dx,ax;
		mov ax,0x0008; out dx,ax;

		mov ax,0xA000; mov es,ax; mov di,screen_offset; //es:di destination vram
		mov	ds,ax; mov si,anim_data	//ds:si source vram
		mov bx,50-16; mov ax,16; mov dx,4;
	}
	_blit_frame:
	asm{
		mov cx,ax; rep movsb; add di,bx; mov cx,ax; rep movsb; add di,bx;
		mov cx,ax; rep movsb; add di,bx; mov cx,ax; rep movsb; add di,bx;
		mov cx,ax; rep movsb; add di,bx; mov cx,ax; rep movsb; add di,bx;
		mov cx,ax; rep movsb; add di,bx; mov cx,ax; rep movsb; add di,bx;
		mov cx,ax; rep movsb; add di,bx; mov cx,ax; rep movsb; add di,bx;
		mov cx,ax; rep movsb; add di,bx; mov cx,ax; rep movsb; add di,bx;
		mov cx,ax; rep movsb; add di,bx; mov cx,ax; rep movsb; add di,bx;
		mov cx,ax; rep movsb; add di,bx; mov cx,ax; rep movsb; add di,bx;
		mov cx,ax; rep movsb; add di,bx; mov cx,ax; rep movsb; add di,bx;
		mov cx,ax; rep movsb; add di,bx; mov cx,ax; rep movsb; add di,bx;
		mov cx,ax; rep movsb; add di,bx; mov cx,ax; rep movsb; add di,bx;
		mov cx,ax; rep movsb; add di,bx; mov cx,ax; rep movsb; add di,bx;
		mov cx,ax; rep movsb; add di,bx; mov cx,ax; rep movsb; add di,bx;
		mov cx,ax; rep movsb; add di,bx; mov cx,ax; rep movsb; add di,bx;
		mov cx,ax; rep movsb; add di,bx; mov cx,ax; rep movsb; add di,bx;
		mov cx,ax; rep movsb; add di,bx; mov cx,ax; rep movsb; add di,bx;

		dec dx; cmp dx,0; jz _end;
		jmp _blit_frame;
	}
	_end:
	asm{
		pop si; pop di; pop ds;
	}
}


// load_16x16 tiles to VRAM
void LT_Load_Tiles(char *file,char* dat_string){
	word VGA_index = 0;
	word f;
	int x,y;
	int plane;
	unsigned char planemask = 0x01;
	unsigned char rowbuf[128];
	unsigned char type = 0;
	unsigned short width = 0;
	unsigned short height = 0;

	LT_Filename = file;
	f = LT_fopen(file,0);
	if(!f) LT_Error("Can't find$",file);
	DAT_Seek2(f,dat_string);

	LT_fread(f,1,&type);
	LT_fread(f,2,&width);
	LT_fread(f,2,&height);

	if(type != 18) LT_Error("Not tileset  $",dat_string);
	if(width != 256) LT_Error("Not tileset  $",dat_string);
	if(height != 208) LT_Error("Not tileset  $",dat_string);

	LT_fread(f,3*24,LT_tileset_palette);
	asm cli;
	asm mov dx,0x03CE; asm mov ax,0x0005; asm out dx,ax;//write mode 0
	//Read image to vram
	for (plane = 0;plane < 4; plane++){
		VGA_index = TILE_VRAM;
		for (x = 0;x < 52; x++){//52x4 = 208
			//fread interferes with writte settings
			LT_fread(f,128,rowbuf);//32x4 (4 tiles)
			//reset every line copied, it is not slow at all, and we save a lot of ram
			asm {
				mov dx,0x03CE;
				mov ax,0x0001; out dx,ax;
				mov ax,0xFF08; out dx,ax;
				mov dx,0x03C4;
				mov ah,planemask; mov al,0x02; out dx,ax;
			}
			_memcpy(&VGA[VGA_index],rowbuf,128);
			VGA_index+=128;
		}
		planemask <<= 1;
	}
	asm sti;

	LT_fclose(f);
}


//Load plain map array
void LT_Load_Map(char *file, char* dat_string){ 
	word f = LT_fopen(file,0);
	if(!f) LT_Error("Can't find$",file);
	DAT_Seek2(f,dat_string);
	//read file
	LT_map_width = 512;
	LT_map_height = 36;
	LT_map_ntiles = 512*36;
	//read actual tile addresses, to avoid calculations
	LT_fread(f,LT_map_ntiles*2,LT_map_data);
	//read collision array
	LT_fread(f,LT_map_ntiles,LT_map_collision);
	LT_fclose(f);
	//copy first chunk of map again at the end, this way we can use 3 update columns 24 tiles each
	// 512*36 = 0x4800 (words); 512*12 = 0x3000 (bytes)
	_memcpy(&LT_map_data[0x4800],LT_map_data,0x3000);
}

extern int LT_wmap;
extern int LT_hmap;
extern byte LT_flipscroll;
void LT_Spawm_Sprites( unsigned short x, unsigned short map_offset);
void draw_map_column(unsigned short x, unsigned short y, unsigned short map_offset, unsigned short ntiles);
int LT_Setting_Map = 0;
void LT_Set_Map(int x, int y){
	int i = 0;
	int j = 0;
	byte tiles = 0;
	Enemies = 0;
	LT_wmap = LT_map_width<<4;
	LT_hmap = LT_map_height<<4;
	LT_map_offset = x;
	LT_map_offset_y = 0;
	LT_map_offset_Endless = 20;
	SCR_X = x<<4;SCR_Y = y<<4;
	if (SCR_X > 8192-320) SCR_X = 8192-320;//512*16
	if (SCR_Y > 576-240) SCR_Y = 576-240;//36*16
	LT_last_x = LT_current_x;
	//draw map
	asm mov dx,0x03CE
	asm mov ax,0x0005	//write mode 0
	asm out dx,ax
	asm mov	dx,0x03CE	  //Set mask register
	asm mov al,0x08
	asm out	dx,al

	LT_Update(0,0);
	tiles = 36;
	LT_VGA_Enable_4Planes();
	for (i = 0;i<400;i+=16) {
		draw_map_column(SCR_X-32+i,pageflip[0],LT_map_offset-2+j,tiles);
		j++;
	}
	j = 0;
	LT_Setting_Map = 1;
	for (i = 0;i<400;i+=16) {
		draw_map_column(SCR_X-32+i,pageflip[1],LT_map_offset-2+j,tiles);
		LT_Spawm_Sprites(SCR_X-32+i,LT_map_offset-2+j);
		j++;
	}
	LT_flipscroll = 0;
	LT_Setting_Map = 0;
	LT_VGA_Return_4Planes();

	LT_Fade_in();
}

void LT_Sprite_Edit_Map(int sprite_number, byte ntile, byte col){
	SPRITE *s = &sprite[sprite_number];
	s->get_item = 1;
	s->ntile_item = ntile;
	s->col_item = col;
}

void LT_Edit_MapTile(word x, word y, byte ntile, byte col){
	word TILE_ADDRESS = TILE_VRAM;
	word tile = (y<<9) + x;
	word y2;
	word y3 = y;
	word ntile2 = TILE_VRAM + (ntile<<5);
	word screen_offset,screen_offset1;
	word lwidth = LT_VRAM_Logical_Width -2;
	x = x<<4;
	y = (y<<4);
	y2 = y + pageflip[1];
	screen_offset = (y*50)+(x>>3);
	screen_offset1 = (y2*50)+(x>>3);
	if (!LT_deleting_text)LT_map_collision[tile] = col;
	
	//asm cli
	//LT_WaitVsyncEnd();
	asm{
		push ds
		push di
		push si
		
		mov dx,SC_INDEX //dx = indexregister
		mov ax,0F02h	//INDEX = MASK MAP, 
		out dx,ax 		//write all the bitplanes.
		mov dx,GC_INDEX //dx = indexregister
		mov ax,008h		
		out dx,ax 
		
		mov 	ax,0A000h
		mov 	ds,ax	
		
		mov		ax,word ptr [ntile2]		//go to desired tile
		mov		si,ax
		
		mov		di,screen_offset		//es:di destination address							
		mov 	ax,0A000h
		mov 	es,ax
		mov		bx,lwidth
		//UNWRAPPED COPY 16x16 TILE LOOP
		movsb; movsb; add di,bx;
		movsb; movsb; add di,bx;
		movsb; movsb; add di,bx;
		movsb; movsb; add di,bx;
		movsb; movsb; add di,bx;
		movsb; movsb; add di,bx;
		movsb; movsb; add di,bx;
		movsb; movsb; add di,bx;
		movsb; movsb; add di,bx;
		movsb; movsb; add di,bx;
		movsb; movsb; add di,bx;
		movsb; movsb; add di,bx;
		movsb; movsb; add di,bx;
		movsb; movsb; add di,bx;
		movsb; movsb; add di,bx;
		movsb; movsb;
		
		//Delete tile on the other page
		sub		si,2*16
		mov		di,screen_offset1		//es:di destination address							
		mov 	ax,0A000h
		mov 	es,ax
		mov		bx,lwidth
		//UNWRAPPED COPY 16x16 TILE LOOP
		movsb; movsb; add di,bx;
		movsb; movsb; add di,bx;
		movsb; movsb; add di,bx;
		movsb; movsb; add di,bx;
		movsb; movsb; add di,bx;
		movsb; movsb; add di,bx;
		movsb; movsb; add di,bx;
		movsb; movsb; add di,bx;
		movsb; movsb; add di,bx;
		movsb; movsb; add di,bx;
		movsb; movsb; add di,bx;
		movsb; movsb; add di,bx;
		movsb; movsb; add di,bx;
		movsb; movsb; add di,bx;
		movsb; movsb; add di,bx;
		movsb; movsb;

		//END
		mov dx,GC_INDEX +1 //dx = indexregister
		mov ax,00ffh		
		out dx,ax 
		
		pop si
		pop di
		pop ds
	}
	//asm sti
	LT_map_data[tile] = TILE_ADDRESS + (ntile<<5);
	if (y3 < 12) LT_map_data[tile+(512*36)] = TILE_ADDRESS + (ntile<<5);
}

void LT_Set_AI_Sprite(byte sprite_number, byte mode, word x, word y, int sx, int sy, word id);
extern byte LT_flipscroll;
extern int LT_scroll_side;
extern int LT_scroll_top;
extern int LT_number_of_ai;

void draw_map_column(unsigned short x, unsigned short y, unsigned short map_offset, unsigned short ntiles){
	unsigned short screen_offset = (y*50)+(x>>3);
	unsigned short *mapdata =  LT_map_data;
	unsigned short lwidth = LT_VRAM_Logical_Width-2;
	map_offset = map_offset<<1;
	asm{
		pusha; push ds;

		mov ax,0A000h; mov ds,ax;

		les bx,[mapdata]
		add bx,map_offset
		mov si,es:[bx]				//ds:si Tile data VRAM address

		mov di,screen_offset		//es:di screen address
		mov cx,ntiles
	}
	//DRAW TILES
	asm{
		// read ALL locals/params first, before BP is touched
		mov  ax,map_offset
		les  bx,[mapdata]
		mov  dx,es            // dx = map segment
		add  bx,ax            // bx = offset of the current tile entry
		mov  ax,lwidth
		push bp
		mov  bp,bx            // bp = running map pointer
		mov  bx,ax            // bx = lwidth
		mov ax,0xA000; mov es,ax; mov ds,ax // es, ds = VRAM

	}

	blit_ega_tile:
	asm{//COPY TILE
		movsb; movsb; add di,bx; movsb; movsb; add di,bx;
		movsb; movsb; add di,bx; movsb; movsb; add di,bx;
		movsb; movsb; add di,bx; movsb; movsb; add di,bx;
		movsb; movsb; add di,bx; movsb; movsb; add di,bx;
		movsb; movsb; add di,bx; movsb; movsb; add di,bx;
		movsb; movsb; add di,bx; movsb; movsb; add di,bx;
		movsb; movsb; add di,bx; movsb; movsb; add di,bx;
		movsb; movsb; add di,bx; movsb; movsb; add di,bx;

		//SET ADDRESS FOR NEXT TILE
		add  bp,1024           // next map entry
		mov  ds,dx            // ds = map segment
		mov  si,ds:[bp]       // explicit ds: override, since BP defaults to SS
		mov  ds,ax            // ds = A000h again

		loop blit_ega_tile;
	}

	end_vga_column:
	asm pop bp;

	_end:
	asm{ pop ds; popa;}

}

void LT_Update_column(unsigned short x, unsigned short y, unsigned short map_offset){
	unsigned short screen_offset = (y*50)+(x>>3);
	unsigned short *mapdata =  LT_map_data;
	unsigned short lwidth = LT_VRAM_Logical_Width-2;
	map_offset = map_offset<<1;
	asm{
		pusha; push ds;

		mov ax,0A000h; mov ds,ax;

		les bx,[mapdata]
		add bx,map_offset
		mov si,es:[bx]				//ds:si Tile data VRAM address
		mov di,screen_offset		//es:di screen address
		mov cx,4					//4x6 = 24 tiles
	}
	//DRAW TILES
	asm{
		// read ALL locals/params first, before BP is touched
		mov  ax,map_offset
		les  bx,[mapdata]
		mov  dx,es            // dx = map segment
		add  bx,ax            // bx = offset of the current tile entry
		mov  ax,lwidth
		push bp
		mov  bp,bx            // bp = running map pointer
		mov  bx,ax            // bx = lwidth
		mov ax,0xA000; mov es,ax; mov ds,ax // es, ds = VRAM
	}

	blit_ega_tile:
	asm{//COPY 6 TILES
		movsb; movsb; add di,bx; movsb; movsb; add di,bx;
		movsb; movsb; add di,bx; movsb; movsb; add di,bx;
		movsb; movsb; add di,bx; movsb; movsb; add di,bx;
		movsb; movsb; add di,bx; movsb; movsb; add di,bx;
		movsb; movsb; add di,bx; movsb; movsb; add di,bx;
		movsb; movsb; add di,bx; movsb; movsb; add di,bx;
		movsb; movsb; add di,bx; movsb; movsb; add di,bx;
		movsb; movsb; add di,bx; movsb; movsb; add di,bx;
		//SET ADDRESS FOR NEXT TILE
		add bp,1024; mov ds,dx; mov si,ds:[bp]; mov ds,ax

		movsb; movsb; add di,bx; movsb; movsb; add di,bx;
		movsb; movsb; add di,bx; movsb; movsb; add di,bx;
		movsb; movsb; add di,bx; movsb; movsb; add di,bx;
		movsb; movsb; add di,bx; movsb; movsb; add di,bx;
		movsb; movsb; add di,bx; movsb; movsb; add di,bx;
		movsb; movsb; add di,bx; movsb; movsb; add di,bx;
		movsb; movsb; add di,bx; movsb; movsb; add di,bx;
		movsb; movsb; add di,bx; movsb; movsb; add di,bx;
		//SET ADDRESS FOR NEXT TILE
		add bp,1024; mov ds,dx; mov si,ds:[bp]; mov ds,ax

		movsb; movsb; add di,bx; movsb; movsb; add di,bx;
		movsb; movsb; add di,bx; movsb; movsb; add di,bx;
		movsb; movsb; add di,bx; movsb; movsb; add di,bx;
		movsb; movsb; add di,bx; movsb; movsb; add di,bx;
		movsb; movsb; add di,bx; movsb; movsb; add di,bx;
		movsb; movsb; add di,bx; movsb; movsb; add di,bx;
		movsb; movsb; add di,bx; movsb; movsb; add di,bx;
		movsb; movsb; add di,bx; movsb; movsb; add di,bx;
		//SET ADDRESS FOR NEXT TILE
		add bp,1024; mov ds,dx; mov si,ds:[bp]; mov ds,ax

		movsb; movsb; add di,bx; movsb; movsb; add di,bx;
		movsb; movsb; add di,bx; movsb; movsb; add di,bx;
		movsb; movsb; add di,bx; movsb; movsb; add di,bx;
		movsb; movsb; add di,bx; movsb; movsb; add di,bx;
		movsb; movsb; add di,bx; movsb; movsb; add di,bx;
		movsb; movsb; add di,bx; movsb; movsb; add di,bx;
		movsb; movsb; add di,bx; movsb; movsb; add di,bx;
		movsb; movsb; add di,bx; movsb; movsb; add di,bx;
		//SET ADDRESS FOR NEXT TILE
		add bp,1024; mov ds,dx; mov si,ds:[bp]; mov ds,ax

		movsb; movsb; add di,bx; movsb; movsb; add di,bx;
		movsb; movsb; add di,bx; movsb; movsb; add di,bx;
		movsb; movsb; add di,bx; movsb; movsb; add di,bx;
		movsb; movsb; add di,bx; movsb; movsb; add di,bx;
		movsb; movsb; add di,bx; movsb; movsb; add di,bx;
		movsb; movsb; add di,bx; movsb; movsb; add di,bx;
		movsb; movsb; add di,bx; movsb; movsb; add di,bx;
		movsb; movsb; add di,bx; movsb; movsb; add di,bx;
		//SET ADDRESS FOR NEXT TILE
		add bp,1024; mov ds,dx; mov si,ds:[bp]; mov ds,ax

		movsb; movsb; add di,bx; movsb; movsb; add di,bx;
		movsb; movsb; add di,bx; movsb; movsb; add di,bx;
		movsb; movsb; add di,bx; movsb; movsb; add di,bx;
		movsb; movsb; add di,bx; movsb; movsb; add di,bx;
		movsb; movsb; add di,bx; movsb; movsb; add di,bx;
		movsb; movsb; add di,bx; movsb; movsb; add di,bx;
		movsb; movsb; add di,bx; movsb; movsb; add di,bx;
		movsb; movsb; add di,bx; movsb; movsb; add di,bx;
		//SET ADDRESS FOR NEXT TILE
		add bp,1024; mov ds,dx; mov si,ds:[bp]; mov ds,ax

		dec cx; jz end_vga_column; jmp blit_ega_tile;
	}

	end_vga_column:
	asm pop bp;
	asm pop ds; asm popa;
}

void LT_Spawm_Sprites( unsigned short x, unsigned short map_offset){
	//Spawm sprites
	if (LT_Sprite_Stack < 8) {
		unsigned char i, j, jstart, type;
		unsigned char nsprite;
		unsigned char xt = x >> 4;               // loop-invariant, hoisted
		word pos_id = xt;                        // (i<<9) + xt, built incrementally
		const unsigned char *p = &LT_map_collision[map_offset];   // pointer walk instead of indexing
		for (i = 0; i < 36; ++i) {
			type = *p;
			if (type >= 16 && type <= 17) {      // fast reject; the common path ends here
				jstart = (type == 16) ? 0 : 4;   // merges both cases into one block
				if (sprite_id_table[pos_id] == 0) {   // check once, not inside the j loop
					for (j = jstart; j < jstart + 3; ++j) {
						nsprite = LT_Active_AI_Sprites[j];
						if (nsprite) {
							LT_Active_AI_Sprites[j] = 0;
							LT_Sprite_Stack_Table[LT_Sprite_Stack] = nsprite;

							if (sprite[nsprite].init == 0)
								LT_Set_AI_Sprite(nsprite, sprite[nsprite].mode,
												 xt, i, LT_scroll_side, 0, pos_id);

								sprite_id_table[pos_id] = 1;
							if (++LT_Sprite_Stack >= 8) return;   // or goto done; see note below
							break;
						}
					}
				}
			}
			p += LT_map_width;
			pos_id += 512;                       // replaces (i<<9) shift every iteration
		}
	}
	//Needs optimization
	/*if (LT_Sprite_Stack < 8){
		int j;
		int nsprite;
		for (i = 0; i <36;i++){
			int sprite0 = LT_map_collision[map_offset]; //sprite0-16 for sprite type
			switch (sprite0){
				case 16:
					for (j = 0; j < 3; j++){
						if (LT_Active_AI_Sprites[j]){
							word pos_id_table = (i<<9) + (x>>4);
							if (sprite_id_table[pos_id_table] == 1) break;
							nsprite = LT_Active_AI_Sprites[j];
							LT_Active_AI_Sprites[j] = 0;
							LT_Sprite_Stack_Table[LT_Sprite_Stack] = nsprite;
							if (sprite[nsprite].init == 0)
								LT_Set_AI_Sprite(nsprite,sprite[nsprite].mode,x>>4,i,LT_scroll_side,0,pos_id_table);//also increase sprite range
							LT_Sprite_Stack++;
							sprite_id_table[pos_id_table] = 1;
							break;
						}
					}
					break;
				case 17:
					for (j = 4; j < 7; j++){
						if (LT_Active_AI_Sprites[j]){
							word pos_id_table = (i<<9) + (x>>4);
							if (sprite_id_table[pos_id_table] == 1) break;
							nsprite = LT_Active_AI_Sprites[j];
							LT_Active_AI_Sprites[j] = 0;
							LT_Sprite_Stack_Table[LT_Sprite_Stack] = nsprite;
							if (sprite[nsprite].init == 0)
								LT_Set_AI_Sprite(nsprite,sprite[nsprite].mode,x>>4,i,LT_scroll_side,0,pos_id_table);
							LT_Sprite_Stack++;
							sprite_id_table[pos_id_table] = 1;
							break;
						}
					}
					break;
			}
			m_offset+= LT_map_width;
		}
	}*/
}

void (*LT_Scroll_Map)(void);

//Only 19 tiles are updated if we hit a new column
//we update the column at other page in the next frame
byte LT_flipscroll = 0;
byte LT_flipscrollv = 0;
int LT_scroll_side = -1;
int LT_scroll_top = -1;
void LT_Scroll_Map_GFX(void){
	LT_current_x = ((SCR_X)>>4)<<4;
	
	// Third frame
	if (LT_flipscroll == 2){
		LT_VGA_Enable_4Planes();
		if (LT_scroll_side == 1){
			if (LT_last_x>24) {
				LT_Update_column(LT_last_x-32,48*16,LT_map_offset-2+(512*12));
				LT_Spawm_Sprites(LT_last_x-32,LT_map_offset-2);
			}
		} else {
			LT_Update_column(LT_last_x+352,48*16,LT_map_offset+22+(512*12));
			LT_Spawm_Sprites(LT_last_x+352,LT_map_offset+22);
		}
		LT_flipscroll = 0;
		LT_VGA_Return_4Planes();
	}
	// Second frame
	if (LT_flipscroll == 1){
		LT_VGA_Enable_4Planes();
		if (LT_scroll_side == 1){
			if (LT_last_x>24) LT_Update_column(LT_last_x-32,24*16,LT_map_offset-2+(512*24));
		} else LT_Update_column(LT_last_x+352,24*16,LT_map_offset+22+(512*24));
		LT_flipscroll = 2;
		LT_VGA_Return_4Planes();
	}
	//Init drawing: First frame
	if (LT_current_x != LT_last_x){
		LT_VGA_Enable_4Planes();
		LT_map_offset = SCR_X>>4;
		if (LT_current_x < LT_last_x){
			LT_scroll_side = 1;
			if (LT_current_x>24) LT_Update_column(LT_current_x-32,0,LT_map_offset-2);
		} else { 
			LT_scroll_side = -1;
			LT_Update_column(LT_current_x+352,0,LT_map_offset+22);
		}
		LT_flipscroll = 1;
		LT_VGA_Return_4Planes();
	}
	
	
	LT_last_x = LT_current_x;
	LT_last_y = LT_current_y;
}


//Fake explosions
int LT_shake[] = {-4,2,0,-1,2,-6,0,4,-3,0,4,-2,0,-1,2,5,-3,4,-4,2,-2,0};
void LT_Screen_Shake(byte mode){
	word px = SCR_X;//Get position
	word py = SCR_Y;//Get position
	byte p = 0;
	if (mode){
		while (p !=22){
			SCR_X = px+LT_shake[p];
			TGA_SCR_X = px+(LT_shake[p]*4);
			LT_Update(0,0);
			p++;
		}
	} else {
		while (p !=22){
			SCR_Y = py+LT_shake[p];
			TGA_SCR_Y = py+(LT_shake[p]*4);
			LT_Update(0,0);
			p++;
		}
	}
}


//PALETTES
void set_palette(unsigned char *palette){
	asm mov	dx,0x03C8; asm mov al,0; asm out dx,al;
	asm lds si,palette
	asm mov dx,0x03C9 //Palete register
	asm mov cx,32*3
	_in_loop:
		asm LODSB //Load byte from DS:SI into AL, then advance SI
		asm out dx,al
		asm loop _in_loop
}


void VGA_ClearPalette(){
	LT_vsync();
	asm {
		mov	dx,0x03C8;
		mov al,0; out dx,al
		inc dx; mov cx,32*3;
		rep out dx,al
	}
}

void LT_Fade_in(){
	int i,j;
	unsigned char *pal = LT_Temp_palette;
	LT_memset(LT_Temp_palette,32*3,0x00);//All colours black
	i = 0;
	
	//Fade in
	while (i < 14){
		LT_vsync();
		LT_Cycle_palcounter = 0;
		LT_Cycle_palframe = 0;

		asm push ds; asm push di; asm push si
	
		asm mov	dx,003c8h
		asm mov al,0
		asm out	dx,al
		asm lds si,pal		//Get palette address in ds:si
		asm les di,pal		//Get palette address in es:di
		asm mov dx,003c9h //Palete register
		asm mov cx,32*3
		asm mov bx,0
		fade_in_loop:
			asm LODSB //Load byte from DS:SI into AL, then advance SI
			asm cmp	al,byte ptr LT_tileset_palette[bx]
			asm jae	pal_is_greater
			asm add al,4
			pal_is_greater:
			asm STOSB //Store byte in AL to ES:DI, then advance DI
			asm out dx,al
			asm inc bx
			asm loop fade_in_loop
		
		asm pop si; asm pop di; asm pop ds
		
		i ++;
		LT_Play_Music();
	}
	set_palette(LT_tileset_palette);
}

void LT_Fade_out(){
	int i,j;
	unsigned char *pal = LT_Temp_palette;
	i = 0;
	
	//Fade to black
	while (i < 15){
		LT_vsync();
		LT_Cycle_palcounter = 0;
		LT_Cycle_palframe = 0;

		asm push ds; asm push di; asm push si
		
		asm mov	dx,003c8h
		asm mov ax,0
		asm out	dx,al
		asm lds si,pal		//Get palette address in ds:si
		asm les di,pal		//Get palette address in es:di
		asm mov dx,003c9h //Palete register
		asm mov cx,32*3
		asm mov bx,0
		fade_out_loop:
			asm LODSB //Load byte from DS:SI into AL, then advance SI
			asm cmp al,0
			asm jz	pal_is_zero
			asm sub al,4
			pal_is_zero:
			asm STOSB //Store byte in AL to ES:DI, then advance DI
			asm out dx,al
			asm loop fade_out_loop
			
		asm pop si; asm pop di; asm pop ds
		
		i ++;

		LT_Play_Music();
	}
	VGA_ClearPalette();
}


//Cycle palette with sub-frame blending (286), dir: 0 = forward, 1 = reverse
void LT_Cycle_palette(byte palnum, byte palsize, byte speed, byte dir){
	//addresses assume same ds segment and palette is not malloc, it is a local array
	unsigned short pbase = (unsigned short) &LT_tileset_palette[palnum*3];
	unsigned short part1 = palsize*3 - LT_Cycle_palframe;  // bytes from frame to end of group
	unsigned short part2 = LT_Cycle_palframe;              // bytes wrapped to start of group
	unsigned short n     = palsize*3;
	unsigned short pend  = pbase + n;
	unsigned short cur   = pbase + LT_Cycle_palframe;
	unsigned short nxt;                            // dither offset accumulator (0..127)

	if (dir == 0){ nxt = cur + 3; if (nxt >= pend) nxt -= n;}
	else {
		if (LT_Cycle_palframe == 0) nxt = cur + n - 3;   // wrap to last color
		else nxt = cur - 3;
	}

	//asm cld
	asm mov dx,0x03C8
	asm mov al,palnum
	asm out dx,al            // start at palette index palnum
	asm inc dx               // 0x03C9 data register

	asm mov si,pbase; asm add si,part2         // si = base + frame
	asm mov cx,part1
	asm rep outsb            // colors frame..end, for 286 only

	asm mov si,pbase; asm mov cx,part2         // cx = 0 -> rep does nothing
	asm rep outsb            // wrapped colors: start..frame-1, for 286 only

	LT_Cycle_palcounter++;
	if (LT_Cycle_palcounter >= speed){
		LT_Cycle_palcounter = 0;
		if (dir == 0){
			LT_Cycle_palframe += 3;
			if (LT_Cycle_palframe >= n) LT_Cycle_palframe = 0;
		} else {
			if (LT_Cycle_palframe == 0) LT_Cycle_palframe = n - 3;
			else                        LT_Cycle_palframe -= 3;
		}
	}
}

void LT_Reset_Palette_Cycle(){
	LT_Cycle_palcounter = 0;
	LT_Cycle_palframe = 0;
}

unsigned char LT2_FONT[256*8] = {
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0x83,0x01,0x29,0x01,0x39,0x01,0x83,0xFF,
	0x83,0x7D,0x55,0x7D,0x45,0x7D,0x83,0xFF,
	0xFF,0x93,0x41,0x01,0x83,0xC7,0xEF,0xFF,
	0xEF,0xC7,0xA3,0x01,0x83,0xC7,0xEF,0xFF,
	0xC7,0xC7,0x29,0x01,0x29,0xEF,0xC7,0xFF,
	0xEF,0xC7,0x83,0x83,0x83,0xEF,0xC7,0xFF,
	0xFF,0xFF,0xE7,0xC3,0xC3,0xE7,0xFF,0xFF,
	0x00,0x00,0x01,0x03,0x07,0x0F,0x1F,0x3F,
	0x00,0x00,0x80,0xC0,0xE0,0xF0,0xF8,0xFC,
	0x3F,0x1F,0x0F,0x07,0x03,0x01,0x00,0x00,
	0xFC,0xF8,0xF0,0xE0,0xC0,0x80,0x00,0x00,
	0x00,0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,0x00,
	0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,
	0xFC,0xFC,0xFC,0xFC,0xFC,0xFC,0xFC,0xFC,
	0xFF,0xBF,0x8F,0x83,0x83,0x8F,0xBF,0xFF,
	0xFF,0xFD,0xF1,0xC1,0xC1,0xF1,0xFD,0xFF,
	0xFF,0xE7,0xC3,0xE7,0xE7,0xC3,0xE7,0xFF,
	0xD7,0xD7,0xD7,0xD7,0xFF,0xD7,0xFF,0xFF,
	0xC1,0x8B,0x8B,0xCB,0xEB,0xEB,0xEB,0xEB,
	0xE3,0xDF,0xE7,0xDB,0xDB,0xE7,0xFB,0xC7,
	0xFF,0xFF,0xFF,0xFF,0x83,0x87,0xBF,0xFF,
	0xE7,0xC3,0x81,0xE7,0xE7,0x81,0xC3,0x81,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xE7,0xC3,0xC3,0xE7,0xFF,0xE7,0xFF,0xFF,
	0xFF,0x99,0x99,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0x93,0x01,0x93,0x01,0x93,0xFF,0xFF,
	0xE7,0xC1,0x9F,0xC3,0xF9,0x83,0xE7,0xFF,
	0xFF,0x99,0x93,0xE7,0xC9,0x99,0xFF,0xFF,
	0x8F,0x27,0x89,0x23,0x33,0x89,0xFF,0xFF,
	0xFF,0xE7,0xE7,0xCF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xF3,0xCF,0x9F,0x9F,0xCF,0xF3,0xFF,
	0xFF,0x9F,0xE7,0xF3,0xF3,0xE7,0x9F,0xFF,
	0xFF,0xFF,0xEF,0xC7,0x01,0x83,0xC7,0x93,
	0xFF,0xFF,0xE7,0xE7,0x81,0xE7,0xE7,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xE7,0xE7,0xCF,
	0xFF,0xFF,0xFF,0xFF,0x00,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xE7,0xE7,0xFF,
	0xFF,0xF9,0xF1,0xE3,0xC7,0x8F,0x9F,0xFF,
	0xFF,0x83,0x39,0x29,0x29,0x39,0x83,0xFF,
	0xFF,0xC7,0xE7,0xE7,0xE7,0xE7,0xC3,0xFF,
	0xFF,0xC3,0x99,0xF9,0xE7,0x9F,0x81,0xFF,
	0xFF,0x81,0xF3,0xE7,0xF9,0x99,0xC3,0xFF,
	0xFF,0xF1,0xE7,0xC9,0x99,0x81,0xF9,0xFF,
	0xFF,0x81,0x9F,0x83,0xF9,0x99,0xC3,0xFF,
	0xFF,0xE3,0xCF,0x93,0x99,0x99,0xC3,0xFF,
	0xFF,0x81,0xF9,0xF3,0xC3,0xCF,0x9F,0xFF,
	0xFF,0x81,0x99,0xC3,0x99,0x99,0xC3,0xFF,
	0xFF,0xC3,0x99,0x99,0xC9,0xF3,0xC7,0xFF,
	0xFF,0xE7,0xE7,0xFF,0xFF,0xE7,0xE7,0xFF,
	0xFF,0xE7,0xE7,0xFF,0xE7,0xE7,0xEF,0xDF,
	0xFF,0xF9,0xE7,0x9F,0xE7,0xF9,0xFF,0xFF,
	0xFF,0xFF,0x81,0xFF,0x81,0xFF,0xFF,0xFF,
	0xFF,0x9F,0xE7,0xF9,0xE7,0x9F,0xFF,0xFF,
	0xFF,0xC3,0x99,0xF9,0xE7,0xFF,0xE7,0xFF,
	0xFF,0xC7,0xBB,0x65,0x55,0x43,0xBF,0xC7,
	0xFF,0xE1,0xE9,0xC9,0xD9,0x81,0xB9,0xFF,
	0xFF,0x81,0x99,0x93,0x99,0x99,0xE3,0xFF,
	0xFF,0xC1,0x9F,0x9F,0x9F,0xCF,0xE1,0xFF,
	0xFF,0x87,0x93,0x99,0x99,0x99,0x83,0xFF,
	0xFF,0x81,0x9F,0x93,0x9F,0x9F,0xC1,0xFF,
	0xFF,0xC1,0x9F,0x93,0x9F,0x9F,0x9F,0xFF,
	0xFF,0xC3,0x9F,0x9F,0x91,0x99,0xC1,0xFF,
	0xFF,0x99,0x99,0x81,0x99,0x99,0x99,0xFF,
	0xFF,0xC3,0xE7,0xE7,0xE7,0xE7,0xC3,0xFF,
	0xFF,0xE1,0xF9,0xF9,0xF9,0x99,0xC3,0xFF,
	0xFF,0x99,0x93,0x87,0x87,0x93,0x99,0xFF,
	0xFF,0x9F,0x9F,0x9F,0x9F,0x9D,0x81,0xFF,
	0xFF,0x39,0x11,0x01,0x29,0x39,0x39,0xFF,
	0xFF,0x99,0x89,0x81,0x91,0x99,0x99,0xFF,
	0xFF,0xC3,0x99,0x99,0x99,0x99,0xC3,0xFF,
	0xFF,0x83,0x99,0x99,0x93,0x9F,0x9F,0xFF,
	0xFF,0xC3,0x99,0x99,0xC3,0xE7,0xC3,0xFF,
	0xFF,0x83,0x99,0x99,0x87,0x93,0x99,0xFF,
	0xFF,0xC1,0x9F,0x83,0xC1,0xF9,0x83,0xFF,
	0xFF,0x81,0xFF,0xE7,0xE7,0xE7,0xE7,0xFF,
	0xFF,0x99,0x99,0x99,0x99,0x91,0xC9,0xFF,
	0xFF,0x91,0x99,0x93,0x87,0x8F,0x9F,0xFF,
	0xFF,0x31,0x39,0x39,0x29,0x01,0x91,0xFF,
	0xFF,0x99,0xC3,0xE7,0xE7,0xC3,0x99,0xFF,
	0xFF,0x99,0x99,0x99,0xC3,0xE7,0xE7,0xFF,
	0xFF,0x81,0xB3,0xE7,0xCF,0x9D,0x81,0xFF,
	0xFF,0xC3,0xCF,0xCF,0xCF,0xCF,0xC3,0xFF,
	0xFF,0x9F,0x8F,0xC7,0xE3,0xF1,0xF9,0xFF,
	0xFF,0xC3,0xF3,0xF3,0xF3,0xF3,0xC3,0xFF,
	0xFF,0xFF,0xEF,0xC7,0x93,0x39,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x81,0xFF,
	0xFF,0xC3,0xE3,0xF3,0xFB,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xC3,0xF9,0xC1,0x99,0xC1,0xFF,
	0xFF,0x9F,0x83,0x99,0x99,0x93,0x87,0xFF,
	0xFF,0xFF,0xC3,0x9F,0x9F,0x9F,0xC3,0xFF,
	0xFF,0xF9,0xC1,0x99,0x99,0x99,0xC1,0xFF,
	0xFF,0xFF,0xC3,0x99,0x81,0x9F,0xC3,0xFF,
	0xFF,0xF1,0xE7,0xC1,0xE7,0xE7,0xE7,0xFF,
	0xFF,0xFF,0xC1,0x99,0xC1,0xF9,0xC3,0xFF,
	0xFF,0x9F,0x83,0x99,0x99,0x99,0x99,0xFF,
	0xFF,0xE7,0xFF,0xC7,0xE7,0xE7,0xE7,0xFF,
	0xFF,0xF3,0xFF,0xE3,0xF3,0xF3,0x87,0xFF,
	0xFF,0x9F,0x99,0x93,0x87,0x93,0x99,0xFF,
	0xFF,0xC7,0xE7,0xE7,0xE7,0xE7,0xE3,0xFF,
	0xFF,0xFF,0x13,0x01,0x29,0x39,0x39,0xFF,
	0xFF,0xFF,0x93,0x89,0x99,0x99,0x99,0xFF,
	0xFF,0xFF,0xC3,0x99,0x99,0x99,0xC3,0xFF,
	0xFF,0xFF,0x83,0x99,0x83,0x9F,0x9F,0xFF,
	0xFF,0xFF,0x83,0x33,0x83,0xF3,0xE1,0xFF,
	0xFF,0xFF,0x93,0x89,0x9F,0x9F,0x9F,0xFF,
	0xFF,0xFF,0xC3,0x9F,0xC7,0xE3,0x87,0xFF,
	0xFF,0xCF,0xC3,0xCF,0xCF,0xCF,0xE3,0xFF,
	0xFF,0xFF,0x99,0x99,0x99,0x91,0xC9,0xFF,
	0xFF,0xFF,0x99,0x93,0x87,0x8F,0x9F,0xFF,
	0xFF,0xFF,0x39,0x39,0x29,0x01,0x91,0xFF,
	0xFF,0xFF,0x99,0xC3,0xE7,0xC3,0x99,0xFF,
	0xFF,0xFF,0x99,0x99,0xC1,0xF9,0xC3,0xFF,
	0xFF,0xFF,0x81,0xB3,0xE7,0xCD,0x81,0xFF,
	0xFF,0xF3,0xE7,0xC7,0xC7,0xE7,0xF3,0xFF,
	0xFF,0xFF,0xF7,0xF7,0xFF,0xF7,0xF7,0xFF,
	0xFF,0xCF,0xE7,0xE3,0xE3,0xE7,0xCF,0xFF,
	0xFF,0xFF,0x8F,0x24,0xF1,0xFF,0xFF,0xFF,
	0xFF,0xC7,0xBB,0x45,0x5D,0x45,0xBB,0xC7,
	0xFF,0xC1,0x9F,0x9F,0x9B,0x9B,0xC1,0xFB,
	0xDB,0xFF,0x99,0x99,0x99,0x91,0xC9,0xFF,
	0xFF,0xFC,0xC3,0x99,0x81,0x9F,0xC3,0xFF,
	0xEF,0xD7,0x87,0xF3,0x83,0x33,0x83,0xFF,
	0xDB,0xFF,0xC3,0xF9,0xC1,0x99,0xC1,0xFF,
	0xFF,0x3F,0xC3,0xF9,0xC1,0x99,0xC1,0xFF,
	0xE3,0xEB,0xC3,0xF9,0xC1,0x99,0xC1,0xFF,
	0xFF,0xFF,0xC3,0x9F,0x97,0xC3,0xF7,0xFF,
	0xEF,0xD7,0xC3,0x99,0x81,0x9F,0xC3,0xFF,
	0xDB,0xFF,0xC3,0x99,0x81,0x9F,0xC3,0xFF,
	0xFF,0x3F,0xC3,0x99,0x81,0x9F,0xC3,0xFF,
	0xDB,0xFF,0xE7,0xFF,0xC7,0xE7,0xE7,0xFF,
	0xEF,0xD7,0xE7,0xFF,0xC7,0xE7,0xE7,0xFF,
	0x9F,0xE7,0xFF,0xC7,0xE7,0xE7,0xE7,0xFF,
	0xDB,0xE1,0xE9,0xC9,0xD9,0x81,0xB9,0xFF,
	0xE3,0xEB,0xE1,0xC9,0xD9,0x81,0xB9,0xFF,
	0xF8,0x81,0x9F,0x93,0x9F,0x9F,0xC1,0xFF,
	0xFF,0xFF,0x83,0x6D,0x61,0x6F,0x91,0xFF,
	0x81,0x6F,0x6F,0x03,0x6F,0x6F,0x61,0xFF,
	0xEF,0xD7,0xC3,0x99,0x99,0x99,0xC3,0xFF,
	0xDB,0xFF,0xC3,0x99,0x99,0x99,0xC3,0xFF,
	0xFF,0x3F,0x87,0x33,0x33,0x33,0x87,0xFF,
	0xEF,0xD7,0x99,0x99,0x99,0x91,0xC9,0xFF,
	0xFF,0x3F,0x99,0x99,0x99,0x91,0xC9,0xFF,
	0xDB,0xFF,0x99,0x99,0xC1,0xF9,0xC3,0xFF,
	0xDB,0xC3,0x99,0x99,0x99,0x99,0xC3,0xFF,
	0xDB,0x99,0x99,0x99,0x99,0x91,0xC9,0xFF,
	0xFF,0xEF,0xC3,0xAF,0xAF,0xC3,0xEF,0xFF,
	0xE7,0xDB,0xDF,0x8F,0xDF,0xDF,0x83,0xFF,
	0xBB,0xD7,0xEF,0x83,0xEF,0x83,0xEF,0xFF,
	0x03,0x7D,0x03,0x5F,0x4F,0x5F,0x67,0xFF,
	0xF7,0xEB,0xEF,0xC7,0xEF,0xEF,0xAF,0xDF,
	0xFF,0xFC,0xC3,0xF9,0xC1,0x99,0xC1,0xFF,
	0xFF,0xF9,0xE7,0xFF,0xC7,0xE7,0xE7,0xFF,
	0xFF,0xFC,0xC3,0x99,0x99,0x99,0xC3,0xFF,
	0xFD,0xFB,0x99,0x99,0x99,0x91,0xC9,0x9F,
	0xC3,0xFF,0x93,0x89,0x99,0x99,0x99,0xFF,
	0xC3,0x99,0x89,0x81,0x91,0x99,0x99,0xFF,
	0xC7,0xB7,0xB7,0xCB,0xFF,0xFF,0xFF,0xFF,
	0xE7,0xDB,0xDB,0xE7,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xE7,0xFF,0xE7,0x9F,0x99,0xC3,0xFF,
	0xFF,0xFF,0xFF,0x81,0xBF,0xBF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0x81,0xFD,0xFD,0xFF,0xFF,
	0x7B,0x77,0x6F,0xD3,0xBD,0x7B,0xF1,0xFF,
	0x7B,0x77,0x6F,0xDB,0xB3,0x61,0xFB,0xFF,
	0xFF,0xE7,0xFF,0xE7,0xC3,0xC3,0xE7,0xFF,
	0xFF,0xC9,0x93,0x27,0x93,0xC9,0xFF,0xFF,
	0xFF,0x93,0xC9,0xE4,0xC9,0x93,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0x85,0x7B,0x7B,0x7B,0x85,0xFF,
	0xC3,0xBD,0xBD,0xA3,0xBD,0xBD,0xA3,0xBF,
	0x01,0xBD,0xBF,0xBF,0xBF,0xBF,0x1F,0xFF,
	0xFF,0xFF,0x81,0xDB,0xDB,0xDB,0xDB,0xFF,
	0x01,0x7D,0xBF,0xDF,0xBF,0x7D,0x01,0xFF,
	0xFF,0xFF,0x80,0x7B,0x7B,0x7B,0x87,0xFF,
	0xFF,0xFF,0x7D,0x7D,0x7D,0x79,0x05,0x7F,
	0xFF,0xFF,0x87,0x5F,0xDF,0xDF,0xEF,0xFF,
	0x83,0xEF,0xC7,0xBB,0xBB,0xC7,0xEF,0x83,
	0xC7,0xBB,0xBB,0x83,0xBB,0xBB,0xC7,0xFF,
	0xC7,0xBB,0x7D,0x7D,0xBB,0xD7,0x11,0xFF,
	0xC7,0xBF,0xCF,0xC7,0xB7,0xB7,0xCF,0xFF,
	0xFF,0xFF,0x99,0x66,0x66,0x99,0xFF,0xFF,
	0xEF,0xC7,0xAB,0xAB,0xAB,0xC7,0xEF,0xFF,
	0xC3,0xBF,0xBF,0x87,0xBF,0xBF,0xC3,0xFF,
	0xFF,0xC7,0xBB,0xBB,0xBB,0xBB,0xBB,0xFF,
	0xFF,0xFF,0xC3,0xFF,0xC3,0xFF,0xC3,0xFF,
	0xFF,0xFF,0xEF,0xC7,0xEF,0xFF,0x83,0xFF,
	0xFF,0xDF,0xEF,0xF7,0xEF,0xDF,0x83,0xFF,
	0xFF,0xEF,0xDF,0xBF,0xDF,0xEF,0x83,0xFF,
	0xF9,0xF6,0xF7,0xF7,0xF7,0xF7,0xF7,0xF7,
	0xEF,0xEF,0xEF,0xEF,0xEF,0xEF,0x6F,0x9F,
	0xFF,0xFF,0xEF,0xFF,0x01,0xFF,0xEF,0xFF,
	0xDF,0xAE,0x75,0xDB,0xAE,0x75,0xFB,0xFF,
	0xEF,0xD7,0xEF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xC7,0xC7,0xC7,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xEF,0xFF,0xFF,0xFF,0xFF,
	0xC0,0xDF,0xDF,0xDF,0xDF,0xDF,0x5F,0xBF,
	0xF9,0xFA,0xFA,0xFA,0xFA,0xFF,0xFF,0xFF,
	0xF8,0xFE,0xF8,0xFB,0xF8,0xFF,0xFF,0xFF,
	0x80,0x80,0x80,0xFF,0x08,0x08,0x08,0xFF,
	0x00,0x7E,0x7E,0x7E,0x7E,0x7E,0x7E,0x00,
};
