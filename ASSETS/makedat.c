#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <dirent.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/stat.h>

struct dirent *direntry;
struct stat s;
unsigned char nfiles = 0;
DIR *folder;
unsigned char *buffer;
uint8_t palette[24*3] = {0};
uint16_t width;
uint16_t height;

int check_ext(unsigned char *name){
	int type = 0;
	int len = strlen((const char*)name);
	uint32_t *ext = (uint32_t*) &name[len-4];
	switch (ext[0]){
		//file extensions
		case 0x504D422E:
		case 0x706D622E: printf("BMP image\n"); type = 1; break;
		case 0x584D542E:
		case 0x786D742E: printf("TMX map\n"); type = 2; break;
		//case 0x4D47562E: .mod
		//case 0x6D67762E: .MOD
		case 0x4D47562E:
		case 0x6D67762E: printf("MUSIC FILE\n"); type = 3; break;
	}
	return type;
}


int Read_BMP_Header(FILE *fp){
	int index;
	int x;
	int offset = 0;
	uint16_t num_colors;
	uint8_t RLE = 0;
	uint16_t DIB_HEADER_SIZE = 0;
	uint8_t pixel_format;
	uint16_t header;
	width = 0;
	height = 0;

	//Read header
	fread(&header,2,1,fp);
	if (header != 0x4D42) {printf("Not a BMP file"); return 0;};
	fseek(fp, 12,SEEK_CUR);
	fread(&DIB_HEADER_SIZE,2,1,fp);
	fseek(fp, 2, SEEK_CUR);
	fread(&width,2,1,fp);
	fseek(fp, 2, SEEK_CUR);
	fread(&height,2,1,fp);
	fseek(fp, 4, SEEK_CUR);
	pixel_format = fgetc(fp);
	fgetc(fp);
	RLE = fgetc(fp);//0 none, 1 = 8 bit, 2 = 4 bit
	fseek(fp, 15,SEEK_CUR);
	fread(&num_colors,2,1,fp);
	//Skip "color space information" (if it is there) added by modern apps like GIMP
	DIB_HEADER_SIZE -=34;
	fseek(fp,DIB_HEADER_SIZE, SEEK_CUR);

	if (num_colors==0)  num_colors=16;
	if (num_colors > 16) {printf("Image has more than 16 colors");return 0;}
	if (pixel_format != 4) {printf("Image must be indexed 4 bit (16 colors)");return 0;}
	if (RLE) {printf("Image must be uncompressed (NO RLE)");return 0;}

	//Load Palette
	for(int i =0;i<16;i++){
		uint8_t col[3];
		fread(col,3,1,fp);
		palette[(int)(i*3+2)] = col[0] >> 2;
		palette[(int)(i*3+1)] = col[1] >> 2;
		palette[(int)(i*3+0)] = col[2] >> 2;
		fgetc(fp);
	}
	// VGA EGA INDEX: 0-7/15-23
	memcpy(&palette[16*3],&palette[8*3],8*3);
	return 1;
}


unsigned short sprite_width = 32;
unsigned short sprite_height = 32;
unsigned short sprite_size = 0;
unsigned char temp_pixels[256*256];
unsigned char final_pixels[256*256];
unsigned char transparent_sprite_color = 0;
uint8_t sprite_palette[16] = {0};


int convert_font(){
	int index;
	unsigned char cell = 0;
	unsigned char pixel_format = 0;
	uint32_t offset = 0;
	uint32_t offset1 = 0;
	unsigned char RLE = 0;
	unsigned short num_colors;
	unsigned short DIB_HEADER_SIZE = 0;
	unsigned short header;
	unsigned short width;
	unsigned short height;
	FILE *file = fopen("font1.bmp","rb");
	FILE *font = fopen("font.bin","w");
	if (!file) {printf("file not found\n");return 0;}
	fread(&header,2,1,file);
	if (header != 0x4D42) return 0;
	fseek(file, 12,SEEK_CUR);
	fread(&DIB_HEADER_SIZE,2,1,file);
	fseek(file, 2, SEEK_CUR);
	fread(&width, 2, 1, file);
	fseek(file, 2, SEEK_CUR);
	fread(&height, 2, 1,  file);
	fseek(file, 4, SEEK_CUR);
	fread(&pixel_format, 1, 1,  file);
	fseek(file, 1, SEEK_CUR);
	fread(&RLE, 1, 1, file);	//0 none, 1 = 8 bit, 2 = 4 bit
	fseek(file, 15,SEEK_CUR);
	fread(&num_colors,2, 1, file);
	//Advance to palette data
	//Skip "color space information" (if it is there) added by modern apps like GIMP
	DIB_HEADER_SIZE -=34;
	fseek(file,DIB_HEADER_SIZE, SEEK_CUR);

	if (num_colors==0)  num_colors=16;
	if (num_colors > 16) return 0;

	//Load Palette
	for(index=0;index<num_colors;index++){
		unsigned char col[4];
		fread(col,1,4,file);
	}

	offset = 16*127;
	for (index = 0; index < 128; index++){
		fread(&temp_pixels[offset],1,16, file);
		offset-=16;
	}
	fclose(file);

	offset= 0;
	fprintf(font,"LT2_FONT[256*8] = {\n");
	for (int y = 0; y < 128; y+=8){
		for (int x = 0; x < 16; x++){
			offset = (y*16)+x;
			for (int b = 0; b < 8; b++){
				unsigned char byte = temp_pixels[offset];
				fprintf(font,"0x%02X,",byte);
				offset+=16;
			}
			fprintf(font,"\n");
		}
	}

	fprintf(font,"};\n");
	fclose(font);
	return 1;
}


int main(int argc, char **argv) {
	uint32_t filesize = 0x0000;
	uint32_t dat_offset = 32 + 2048;
	uint32_t start_image_data = 0;
	uint32_t file_pos = 0;
    unsigned char i, arg;
	unsigned char ssize = 0;
	unsigned char extract = 0;
	unsigned char *extract_file;
	unsigned char number = argc-1;
	//IMAGE TYPE
	//1=320x240, image; 2=256x208, tileset; 3=256xAny, sprites; 4=128x128, bkg animation
	//Sprite sheets will never be 240 or 208 tall (not divisible by 32)
	unsigned char image_type = 0;
	uint16_t TILE_VRAM = 0xE4E8;//1172x50 fixed in engine
	int plane_size = 0;
	uint32_t _offset = 0;
    FILE *dat;
	FILE *file;
	unsigned char is_folder = 0;
	buffer = (unsigned char*) calloc(1,65535*4);
	
	//convert_sprite("player.bmp");
	//return 0;

	if (!argv[1]) {printf("\nusage\n- Create dat: makedat file1 file2 ...\n- Extract dat: makedat -e FILE.DAT"); return 0;}
	if (argc > 64) {printf("\nOnly 64 files are supported\n"); return 0;}
	extract_file = (unsigned char*)argv[1];
	if ((extract_file[0] == '-')&&(extract_file[1] == 'e')) {
		printf("\nextract\n"); 
		extract = 1;
	}
	printf("\n");
		//Is it a folder?
		stat(argv[1],&s);
		if (s.st_mode & S_IFDIR) is_folder = 1;
		
		//CREATE FILE
		dat = fopen("DATA.DAT","wb");
		
		fprintf(dat,"LDAT  LT-ENGINE FILES:          ");
		fseek(dat, 22, SEEK_SET); fwrite(&number,1,1,dat);

		if (!is_folder){
			for (arg=1; arg<argc; arg++) {
				int i = 0;
				int tilecount = 0;
				int namepos = 0;
				int type = 0;
				int index = 0;
				char line[128];
				unsigned char start_map_data = 0;
				unsigned char *name = (unsigned char*)argv[arg];
				filesize = 0;
				//if dropped file (windows/linux) remove path
				for (i = 0; i < strlen((const char*)name);i++){
					if (name[i] == 0x5C || name[i] == 0x2F) namepos = i+1;
				}
				memmove(&name[0],&name[namepos],16);
				//Print file name
				fseek(dat, arg*32, SEEK_SET);
				fprintf(dat,"%s",name);
				if ((strlen((const char*)name))>12){printf("\nCan't use names bigger than 8 characters %s\n",name); return 0;}
				//Check file type
				type = check_ext(name);
				//Read file to insert
				file = fopen((const char*)name,"rb");
				if (!file) {printf("\nCan't find %s\n",name); return 0;}
				switch (type){
					case 0:
					case 1://BMP
						Read_BMP_Header(file);
						//Animation
						if (width == 512 && height == 384) {
							plane_size = width/8*height;
							fseek(file,(width>>1)*(height-1), SEEK_CUR);//start of BMP pixels
							start_image_data = ftell(file);
							printf("\nANIMATION\n");
							image_type = 4; buffer[0] = 20;
							memcpy(&buffer[1],&width,2);
							memcpy(&buffer[3],&height,2);
							_offset = 5;
							filesize = 5 + (plane_size*4); // + 4 planes
							//parse 128x128 frames and store sequentialy
							for (int ty = 0; ty < height; ty+=128){
								for (int tx = 0; tx < width/2; tx+=(128/2)){
									//get tile
									fseek(file,start_image_data-(ty*width/2)+tx, SEEK_SET);//start of next scanline
									for (int y = 0; y < 128; y++){
										for (int x = 0; x < 16; x++){
											uint8_t _plane0 = 0; uint8_t _plane1 = 0;
											uint8_t _plane2 = 0; uint8_t _plane3 = 0;
											//it is a 4 bit BMP, every byte contains 2 pixels
											// 4 bytes contain 8 pixels
											uint8_t p0 = fgetc(file); uint8_t p1 = fgetc(file);
											uint8_t p2 = fgetc(file); uint8_t p3 = fgetc(file);
											_plane0 = ((p0&0x10)<<3)|((p0&0x01)<<6)|((p1&0x10)<<1)|((p1&0x01)<<4)|
											((p2&0x10)>>1)|((p2&0x01)<<2)|((p3&0x10)>>3)|(p3&0x01);
											_plane1 = ((p0&0x20)<<2)|((p0&0x02)<<5)|(p1&0x20   )|((p1&0x02)<<3)|
											((p2&0x20)>>2)|((p2&0x02)<<1)|((p3&0x20)>>4)|((p3&0x02)>>1);
											_plane2 = ((p0&0x40)<<1)|((p0&0x04)<<4)|((p1&0x40)>>1)|((p1&0x04)<<2)|
											((p2&0x40)>>3)|(p2&0x04)|((p3&0x40)>>5)|((p3&0x04)>>2);
											_plane3 = (p0&0x80)|((p0&0x08)<<3)|((p1&0x80)>>2)|((p1&0x08)<<1)|
											((p2&0x80)>>4)|((p2&0x08)>>1)|((p3&0x80)>>6)|((p3&0x08)>>3);
											buffer[    0        + _offset] = _plane0;
											buffer[plane_size   + _offset] = _plane1;
											buffer[plane_size*2 + _offset] = _plane2;
											buffer[plane_size*3 + _offset] = _plane3;
											_offset++;
										}
										fseek(file,-(width/2)-64, SEEK_CUR);//start of next scanline
									}
								}
							}

						}
						//image
						else if (width == 320 && height == 240) {
							plane_size = width/8*height;
							fseek(file,(width>>1)*(height-1), SEEK_CUR);//start of BMP pixels
							start_image_data = ftell(file);

							printf("\nIMAGE (320x240)\n");
							image_type = 1; buffer[0] = 17;
							memcpy(&buffer[1],&width,2);
							memcpy(&buffer[3],&height,2);
							memcpy(&buffer[5],palette,3*24);
							_offset = (3*24)+5;
							filesize = 1 + 4 + (3*24) + (plane_size*4); //palette loaded + 4 planes
							//fread(buffer,pixels);
							for (int y = 0; y < height; y++){
								for (int x = 0; x < width/8; x++){
									uint8_t _plane0 = 0; uint8_t _plane1 = 0;
									uint8_t _plane2 = 0; uint8_t _plane3 = 0;
									//it is a 4 bit BMP, every byte contains 2 pixels
									// 4 bytes contain 8 pixels
									uint8_t p0 = fgetc(file); uint8_t p1 = fgetc(file);
									uint8_t p2 = fgetc(file); uint8_t p3 = fgetc(file);
									_plane0 = ((p0&0x10)<<3)|((p0&0x01)<<6)|((p1&0x10)<<1)|((p1&0x01)<<4)|
									((p2&0x10)>>1)|((p2&0x01)<<2)|((p3&0x10)>>3)|(p3&0x01);
									_plane1 = ((p0&0x20)<<2)|((p0&0x02)<<5)|(p1&0x20   )|((p1&0x02)<<3)|
									((p2&0x20)>>2)|((p2&0x02)<<1)|((p3&0x20)>>4)|((p3&0x02)>>1);
									_plane2 = ((p0&0x40)<<1)|((p0&0x04)<<4)|((p1&0x40)>>1)|((p1&0x04)<<2)|
									((p2&0x40)>>3)|(p2&0x04)|((p3&0x40)>>5)|((p3&0x04)>>2);
									_plane3 = (p0&0x80)|((p0&0x08)<<3)|((p1&0x80)>>2)|((p1&0x08)<<1)|
									((p2&0x80)>>4)|((p2&0x08)>>1)|((p3&0x80)>>6)|((p3&0x08)>>3);
									buffer[    0        + _offset] = _plane0;
									buffer[plane_size   + _offset] = _plane1;
									buffer[plane_size*2 + _offset] = _plane2;
									buffer[plane_size*3 + _offset] = _plane3;
									_offset++;
								}
								fseek(file,(width>>1)*-2, SEEK_CUR);//start of next scanline
							}
						}
						//tiles
						else if (width == 256 && height == 208) {
							plane_size = width/8*height;
							fseek(file,(width>>1)*(height-1), SEEK_CUR);//start of BMP pixels
							start_image_data = ftell(file);

							printf("\nTILESET\n");
							image_type = 2; buffer[0] = 18;
							memcpy(&buffer[1],&width,2);
							memcpy(&buffer[3],&height,2);
							memcpy(&buffer[5],palette,3*24);
							_offset = (3*24)+5;
							filesize = 1 + 4 + (3*24) + (plane_size*4); //palette loaded + 4 planes
							//parse 16x16 tiles and store sequentialy
							for (int ty = 0; ty < height; ty+=16){
								for (int tx = 0; tx < width/2; tx+=(16/2)){
									//get tile
									fseek(file,start_image_data-(ty*width/2)+tx, SEEK_SET);//start of next scanline
									for (int y = 0; y < 16; y++){
										for (int x = 0; x < 2; x++){
											uint8_t _plane0 = 0; uint8_t _plane1 = 0;
											uint8_t _plane2 = 0; uint8_t _plane3 = 0;
											//it is a 4 bit BMP, every byte contains 2 pixels
											// 4 bytes contain 8 pixels
											uint8_t p0 = fgetc(file); uint8_t p1 = fgetc(file);
											uint8_t p2 = fgetc(file); uint8_t p3 = fgetc(file);
											_plane0 = ((p0&0x10)<<3)|((p0&0x01)<<6)|((p1&0x10)<<1)|((p1&0x01)<<4)|
											((p2&0x10)>>1)|((p2&0x01)<<2)|((p3&0x10)>>3)|(p3&0x01);
											_plane1 = ((p0&0x20)<<2)|((p0&0x02)<<5)|(p1&0x20   )|((p1&0x02)<<3)|
											((p2&0x20)>>2)|((p2&0x02)<<1)|((p3&0x20)>>4)|((p3&0x02)>>1);
											_plane2 = ((p0&0x40)<<1)|((p0&0x04)<<4)|((p1&0x40)>>1)|((p1&0x04)<<2)|
											((p2&0x40)>>3)|(p2&0x04)|((p3&0x40)>>5)|((p3&0x04)>>2);
											_plane3 = (p0&0x80)|((p0&0x08)<<3)|((p1&0x80)>>2)|((p1&0x08)<<1)|
											((p2&0x80)>>4)|((p2&0x08)>>1)|((p3&0x80)>>6)|((p3&0x08)>>3);
											buffer[    0        + _offset] = _plane0;
											buffer[plane_size   + _offset] = _plane1;
											buffer[plane_size*2 + _offset] = _plane2;
											buffer[plane_size*3 + _offset] = _plane3;
											_offset++;
										}
										fseek(file,-(width/2)-8, SEEK_CUR);//start of next scanline
									}
								}
							}
						}
						//sprite 32x32
						else if (width == 256 && height != 208) {
							uint8_t key = 0;
							int16_t jump_back = 0;
							int check_palette = 0;
							uint32_t _offset = 0;
							unsigned char cell = 0;
							uint32_t offset = 0;
							uint32_t offset1 = 0;
							printf("SPRITE %s; ",name);
							image_type = 3;
							buffer[_offset++] = 19;
							buffer[_offset++] = (width/32)*(height/32);//number of frames
							filesize+=2;

							offset = (width/2)*(height-1);
							for (index = 0; index < height; index++){
								fread(&temp_pixels[offset],1,width/2, file);
								offset-=(width/2);
							}
							offset= 0;
							for (index = 0; index < width/2*height; index++){
								unsigned char c = temp_pixels[index];
								final_pixels[offset  ] = (c >>4);
								final_pixels[offset+1] = (c & 0x0F);
								offset+=2;
							}
							////////////////
							//parse frames//
							////////////////
							transparent_sprite_color = final_pixels[0];
							printf("TRANSPARENT = %i\n",transparent_sprite_color);
							for (int y0 = 0; y0 < height; y0+=32){
								for (int x0 = 0; x0 < width; x0+= 32){
									uint32_t start_frame = _offset;
									_offset+=2;//skip sprite size
									filesize+=2;
									check_palette = 0;
									memset(sprite_palette,0,16);
									offset = ( y0 * width)+x0;
									offset1 = 0;
									//get used colors
									for (int y = 0; y < 32; y++){
										for (int x = 0; x < 32; x++){
											int found_new_color = 1;
											unsigned char pixel = final_pixels[offset++];
											if (pixel == transparent_sprite_color) continue;
											for (int i = 0; i < 16; i++){
												if (pixel == sprite_palette[i]) found_new_color = 0;
											}
											if (found_new_color) {
												if (check_palette > 15) {printf("PAL colors > 16)");return 0;}
												sprite_palette[1+check_palette] = pixel;
												check_palette++;
											}
										}
										offset+=width-32;
									}

									/////////////////////
									//create frame code//
									/////////////////////
									sprite_size = 0;
									//parse all colors
									for (int colors = 0; colors < check_palette+1; colors++){
										offset = ( y0 * width)+x0;
										//printf("mov al,0x%02X; out dx,al;\n",sprite_palette[colors]);
										buffer[_offset++]=0xB0;buffer[_offset++] = sprite_palette[colors];
										buffer[_offset++]=0xEE;
										sprite_size+=3;
										for (int y = 0; y < 32; y++){
											for (int x = 0; x < 4; x++){
												uint8_t mask = 0;
												uint16_t offset_vram = (y*50)+x;
												for (int p = 7; p > -1; p--){
													unsigned char pixel = final_pixels[offset++];
													if (pixel == transparent_sprite_color) continue;
													if (pixel != sprite_palette[colors]) continue;
													else mask |= 1<<p;
												}
												if (mask) {
													if (offset_vram > 127) {
														buffer[_offset++]=0x26;buffer[_offset++]=0x80;buffer[_offset++]=0xA5;
														buffer[_offset++]=offset_vram&0x00FF;
														buffer[_offset++]=offset_vram>>8;
														buffer[_offset++]=mask;
														sprite_size+=6;
														//printf("and byte ptr es:[di+0x%04X],0x%02x\n",offset_vram,mask);
													} else {
														buffer[_offset++]=0x26;buffer[_offset++]=0x80;buffer[_offset++]=0x65;
														buffer[_offset++]=offset_vram&0x00FF;
														buffer[_offset++]=mask;
														sprite_size+=5;
														//printf("and byte ptr es:[di+0x%02X],0x%02x\n",(uint8_t)offset_vram,mask);
													}
												}
											}
											offset+=width-32;
										}
									}

									//set right mask
									//printf("SET RIGHT MASK\n");
									buffer[_offset++]=0x4A;buffer[_offset++]=0x49;	// dec dx; dec cx;
									buffer[_offset++]=0x74;buffer[_offset++]=13;	// jz _end; skip next 13 bytes, to CB
									buffer[_offset++]=0x47;							// inc di;
									buffer[_offset++]=0xB0;buffer[_offset++]=0x08;	// mov al,0x08;
									buffer[_offset++]=0x88;buffer[_offset++]=0xFC;	// mov ah,bh;
									buffer[_offset++]=0xEF;							// out dx,ax; (bh right mask)
									buffer[_offset++]=0xB0;buffer[_offset++]=0x00;	// mov al,0x00;
									buffer[_offset++]=0xEE;buffer[_offset++]=0x42;	// out dx,al; inc dx; select Set/Reset reg
									//loop
									//printf("LOOP\n");
									sprite_size+=(14+3); jump_back = -sprite_size;//+3 to add jmp int16
									//jmp -int16_t to start;
									buffer[_offset++]=0xE9;
									buffer[_offset++]=jump_back&0x00FF; buffer[_offset++]=jump_back>>8;
									//Insert far return for every frame
									buffer[_offset++]=0xCB;
									sprite_size+=1;

									//printf("%i\n",sprite_size);
									buffer[start_frame] = sprite_size&0x00FF;
									buffer[start_frame+1] = sprite_size>>8;
									filesize+=sprite_size;
								}
							}
						}
						//sprite 128x128
						else if (width == 128 && height == 128) {
							uint8_t key = 0;
							int16_t jump_back = 0;
							int check_palette = 0;
							uint32_t _offset = 0;
							unsigned char cell = 0;
							uint32_t offset = 0;
							uint32_t offset1 = 0;
							printf("SPRITE 64 %s; ",name);
							image_type = 3;
							buffer[_offset++] = 15;
							buffer[_offset++] = (width/64)*(height/64);//number of frames
							filesize+=2;

							offset = (width/2)*(height-1);
							for (index = 0; index < height; index++){
								fread(&temp_pixels[offset],1,width/2, file);
								offset-=(width/2);
							}
							offset= 0;
							for (index = 0; index < width/2*height; index++){
								unsigned char c = temp_pixels[index];
								final_pixels[offset  ] = (c >>4);
								final_pixels[offset+1] = (c & 0x0F);
								offset+=2;
							}
							////////////////
							//parse frames//
							////////////////
							transparent_sprite_color = final_pixels[0];
							printf("TRANSPARENT = %i\n",transparent_sprite_color);
							for (int y0 = 0; y0 < height; y0+=64){
								for (int x0 = 0; x0 < width; x0+= 64){
									uint32_t start_frame = _offset;
									_offset+=2;//skip sprite size
									filesize+=2;
									check_palette = 0;
									memset(sprite_palette,0,16);
									offset = (y0 * width)+x0;
									offset1 = 0;
									//get used colors
									for (int y = 0; y < 64; y++){
										for (int x = 0; x < 64; x++){
											int found_new_color = 1;
											unsigned char pixel = final_pixels[offset++];
											if (pixel == transparent_sprite_color) continue;
											for (int i = 0; i < 16; i++){
												if (pixel == sprite_palette[i]) found_new_color = 0;
											}
											if (found_new_color) {
												if (check_palette > 15) {printf("PAL colors > 16)");return 0;}
												sprite_palette[1+check_palette] = pixel;
												check_palette++;
											}
										}
										offset+=width-64;
									}

									/////////////////////
									//create frame code//
									/////////////////////
									sprite_size = 0;
									//parse all colors
									for (int colors = 0; colors < check_palette+1; colors++){
										offset = (y0 * width)+x0;
										//printf("mov al,0x%02X; out dx,al;\n",sprite_palette[colors]);
										buffer[_offset++]=0xB0;buffer[_offset++] = sprite_palette[colors];
										buffer[_offset++]=0xEE;
										sprite_size+=3;
										for (int y = 0; y < 64; y++){
											for (int x = 0; x < 8; x++){
												uint8_t mask = 0;
												uint16_t offset_vram = (y*50)+x;
												for (int p = 7; p > -1; p--){
													unsigned char pixel = final_pixels[offset++];
													if (pixel == transparent_sprite_color) continue;
													if (pixel != sprite_palette[colors]) continue;
													else mask |= 1<<p;
												}
												if (mask) {
													if (offset_vram > 127) {
														buffer[_offset++]=0x26;buffer[_offset++]=0x80;buffer[_offset++]=0xA5;
														buffer[_offset++]=offset_vram&0x00FF;
														buffer[_offset++]=offset_vram>>8;
														buffer[_offset++]=mask;
														sprite_size+=6;
														//printf("and byte ptr es:[di+0x%04X],0x%02x\n",offset_vram,mask);
													} else {
														buffer[_offset++]=0x26;buffer[_offset++]=0x80;buffer[_offset++]=0x65;
														buffer[_offset++]=offset_vram&0x00FF;
														buffer[_offset++]=mask;
														sprite_size+=5;
														//printf("and byte ptr es:[di+0x%02X],0x%02x\n",(uint8_t)offset_vram,mask);
													}
												}
											}
											offset+=width-64;
										}
									}

									//set right mask
									//printf("SET RIGHT MASK\n");
									buffer[_offset++]=0x4A;buffer[_offset++]=0x49;	// dec dx; dec cx;
									buffer[_offset++]=0x74;buffer[_offset++]=13;	// jz _end; skip next 13 bytes, to CB
									buffer[_offset++]=0x47;							// inc di;
									buffer[_offset++]=0xB0;buffer[_offset++]=0x08;	// mov al,0x08;
									buffer[_offset++]=0x88;buffer[_offset++]=0xFC;	// mov ah,bh;
									buffer[_offset++]=0xEF;							// out dx,ax; (bh right mask)
									buffer[_offset++]=0xB0;buffer[_offset++]=0x00;	// mov al,0x00;
									buffer[_offset++]=0xEE;buffer[_offset++]=0x42;	// out dx,al; inc dx; select Set/Reset reg
									//loop
									//printf("LOOP\n");
									sprite_size+=(14+3); jump_back = -sprite_size;//+3 to add jmp int16
									//jmp -int16_t to start;
									buffer[_offset++]=0xE9;
									buffer[_offset++]=jump_back&0x00FF; buffer[_offset++]=jump_back>>8;
									//Insert far return for every frame
									buffer[_offset++]=0xCB;
									sprite_size+=1;

									//printf("%i\n",sprite_size);
									buffer[start_frame] = sprite_size&0x00FF;
									buffer[start_frame+1] = sprite_size>>8;
									filesize+=sprite_size;
								}
							}
						}
						else {
							printf("Image must be / Imagen debe ser:\n");
							printf(" -ANIMATION 512x384\n");
							printf(" -IMAGE 320x240\n");
							printf(" -TILES 512x384\n");
							printf(" -SPRITE (32x32) 256x128\n");
							printf(" -SPRITE (64x64) 128x128\n");
							return 0;
						}
					break;
					case 3://VGM or MOD
						fseek(file, 0, SEEK_END);
						filesize = ftell(file);
						rewind(file);
						fread(buffer,filesize,1,file);
					break;
					case 2://store tmx maps more efficiently
						while(!start_map_data){	//read lines 
							memset(line, 0, 64);
							fgets(line, 64, file);
							// <tileset firstgid="number" name... two lines like this
							// last one is first collision tile index, we will subtract it
							if((line[1] == '<')&&(line[2] == 't')) {
								int n = sscanf(line, " <tileset firstgid=\"%d\" name", &tilecount);
								if (!n) {printf("Not a tiled 1.1 map"); return 0;}
							}
							//  <data encoding="csv">
							if((line[2] == '<')&&(line[3] == 'd')) start_map_data = 1;
						}
						start_map_data = 0;
						filesize = 0;
						//fseek(file, 0, SEEK_SET);
						//read map in bytes
						for (index = 0; index < 512*36; index++){
							int tile = 0;
							uint16_t *buf = (uint16_t*) &buffer[filesize];
							fscanf(file, "%d,",&tile); if (tile) tile--; // base 0
							buf[0] = TILE_VRAM + (uint16_t)((tile)<<5);
							//printf("%i,",tile);
							filesize+=2;
						}
						//advance to bkg map
						while(!start_map_data){	//read lines 
							memset(line, 0, 64);
							fgets(line, 64, file);
							if((line[2] == '<')&&(line[3] == 'd')) start_map_data = 1;//<data encoding="csv">
						}
						//read bkg map in bytes
						for (index = 0; index < 512*36; index++){
							int tile = 0;
							fscanf(file, "%d,",&tile);
							tile-=tilecount; //subtract, leave base 1, collision 0 not used
							buffer[filesize++] = (unsigned char)tile;
						}
					break;
				}
				//Write file offset in data, and file size
				file_pos = dat_offset;
				fseek(dat, (arg*32)+16, SEEK_SET);
				fwrite(&file_pos,4,1,dat);
				fwrite(&filesize,4,1,dat);
				//Write actual file
				fseek(dat,file_pos, SEEK_SET);
				fwrite(buffer,filesize,1,dat);
				dat_offset += filesize;
				fclose(file);
				nfiles++;
			}
		} else {
			//READ FOLDER
			chdir(argv[1]);
			folder = opendir(".");
			nfiles = 1;
			while((direntry = readdir(folder)) != NULL){
				stat(direntry->d_name,&s);
				if (!(s.st_mode & S_IFDIR)) {
					int i = 0;
					int tilecount = 0;
					int namepos = 0;
					int type = 0;
					int index = 0;
					char line[128];
					unsigned char start_map_data = 0;
					unsigned char *name = (unsigned char*)direntry->d_name;
					//if dropped file (windows) remove path
					for (i = 0; i < strlen((const char*)name);i++){
						if (name[i] == 0x5C) namepos = i+1;
					}
					memmove(&name[0],&name[namepos],16);
					//Print file name
					fseek(dat, nfiles*32, SEEK_SET);
					fprintf(dat,"%s",name);
					if ((strlen((const char*)name))>12){printf("\nCan't use names bigger than 8 characters %s\n",name); return 0;}
					//Check file type
					type = check_ext(name);
					//Read file to insert
					file = fopen((const char*)name,"rb");
					if (!file) {printf("\nCan't find %s\n",name); return 0;}
					switch (type){
						case 0:
						case 1://BMP
							Read_BMP_Header(file);
							//Animation
							if (width == 512 && height == 384) {
								plane_size = width/8*height;
								fseek(file,(width>>1)*(height-1), SEEK_CUR);//start of BMP pixels
								start_image_data = ftell(file);
								printf("\nANIMATION\n");
								image_type = 4; buffer[0] = 20;
								memcpy(&buffer[1],&width,2);
								memcpy(&buffer[3],&height,2);
								_offset = 5;
								filesize = 5 + (plane_size*4); // + 4 planes
								//parse 128x128 frames and store sequentialy
								for (int ty = 0; ty < height; ty+=128){
									for (int tx = 0; tx < width/2; tx+=(128/2)){
										//get tile
										fseek(file,start_image_data-(ty*width/2)+tx, SEEK_SET);//start of next scanline
										for (int y = 0; y < 128; y++){
											for (int x = 0; x < 16; x++){
												uint8_t _plane0 = 0; uint8_t _plane1 = 0;
												uint8_t _plane2 = 0; uint8_t _plane3 = 0;
												//it is a 4 bit BMP, every byte contains 2 pixels
												// 4 bytes contain 8 pixels
												uint8_t p0 = fgetc(file); uint8_t p1 = fgetc(file);
												uint8_t p2 = fgetc(file); uint8_t p3 = fgetc(file);
												_plane0 = ((p0&0x10)<<3)|((p0&0x01)<<6)|((p1&0x10)<<1)|((p1&0x01)<<4)|
												((p2&0x10)>>1)|((p2&0x01)<<2)|((p3&0x10)>>3)|(p3&0x01);
												_plane1 = ((p0&0x20)<<2)|((p0&0x02)<<5)|(p1&0x20   )|((p1&0x02)<<3)|
												((p2&0x20)>>2)|((p2&0x02)<<1)|((p3&0x20)>>4)|((p3&0x02)>>1);
												_plane2 = ((p0&0x40)<<1)|((p0&0x04)<<4)|((p1&0x40)>>1)|((p1&0x04)<<2)|
												((p2&0x40)>>3)|(p2&0x04)|((p3&0x40)>>5)|((p3&0x04)>>2);
												_plane3 = (p0&0x80)|((p0&0x08)<<3)|((p1&0x80)>>2)|((p1&0x08)<<1)|
												((p2&0x80)>>4)|((p2&0x08)>>1)|((p3&0x80)>>6)|((p3&0x08)>>3);
												buffer[    0        + _offset] = _plane0;
												buffer[plane_size   + _offset] = _plane1;
												buffer[plane_size*2 + _offset] = _plane2;
												buffer[plane_size*3 + _offset] = _plane3;
												_offset++;
											}
											fseek(file,-(width/2)-64, SEEK_CUR);//start of next scanline
										}
									}
								}

							}
							//image
							else if (width == 320 && height == 240) {
								plane_size = width/8*height;
								fseek(file,(width>>1)*(height-1), SEEK_CUR);//start of BMP pixels
								start_image_data = ftell(file);

								printf("\nIMAGE (320x240)\n");
								image_type = 1; buffer[0] = 17;
								memcpy(&buffer[1],&width,2);
								memcpy(&buffer[3],&height,2);
								memcpy(&buffer[5],palette,3*24);
								_offset = (3*24)+5;
								filesize = 1 + 4 + (3*24) + (plane_size*4); //palette loaded + 4 planes
								//fread(buffer,pixels);
								for (int y = 0; y < height; y++){
									for (int x = 0; x < width/8; x++){
										uint8_t _plane0 = 0; uint8_t _plane1 = 0;
										uint8_t _plane2 = 0; uint8_t _plane3 = 0;
										//it is a 4 bit BMP, every byte contains 2 pixels
										// 4 bytes contain 8 pixels
										uint8_t p0 = fgetc(file); uint8_t p1 = fgetc(file);
										uint8_t p2 = fgetc(file); uint8_t p3 = fgetc(file);
										_plane0 = ((p0&0x10)<<3)|((p0&0x01)<<6)|((p1&0x10)<<1)|((p1&0x01)<<4)|
										((p2&0x10)>>1)|((p2&0x01)<<2)|((p3&0x10)>>3)|(p3&0x01);
										_plane1 = ((p0&0x20)<<2)|((p0&0x02)<<5)|(p1&0x20   )|((p1&0x02)<<3)|
										((p2&0x20)>>2)|((p2&0x02)<<1)|((p3&0x20)>>4)|((p3&0x02)>>1);
										_plane2 = ((p0&0x40)<<1)|((p0&0x04)<<4)|((p1&0x40)>>1)|((p1&0x04)<<2)|
										((p2&0x40)>>3)|(p2&0x04)|((p3&0x40)>>5)|((p3&0x04)>>2);
										_plane3 = (p0&0x80)|((p0&0x08)<<3)|((p1&0x80)>>2)|((p1&0x08)<<1)|
										((p2&0x80)>>4)|((p2&0x08)>>1)|((p3&0x80)>>6)|((p3&0x08)>>3);
										buffer[    0        + _offset] = _plane0;
										buffer[plane_size   + _offset] = _plane1;
										buffer[plane_size*2 + _offset] = _plane2;
										buffer[plane_size*3 + _offset] = _plane3;
										_offset++;
									}
									fseek(file,(width>>1)*-2, SEEK_CUR);//start of next scanline
								}
							}
							//tiles
							else if (width == 256 && height == 208) {
								plane_size = width/8*height;
								fseek(file,(width>>1)*(height-1), SEEK_CUR);//start of BMP pixels
								start_image_data = ftell(file);

								printf("\nTILESET\n");
								image_type = 2; buffer[0] = 18;
								memcpy(&buffer[1],&width,2);
								memcpy(&buffer[3],&height,2);
								memcpy(&buffer[5],palette,3*24);
								_offset = (3*24)+5;
								filesize = 1 + 4 + (3*24) + (plane_size*4); //palette loaded + 4 planes
								//parse 16x16 tiles and store sequentialy
								for (int ty = 0; ty < height; ty+=16){
									for (int tx = 0; tx < width/2; tx+=(16/2)){
										//get tile
										fseek(file,start_image_data-(ty*width/2)+tx, SEEK_SET);//start of next scanline
										for (int y = 0; y < 16; y++){
											for (int x = 0; x < 2; x++){
												uint8_t _plane0 = 0; uint8_t _plane1 = 0;
												uint8_t _plane2 = 0; uint8_t _plane3 = 0;
												//it is a 4 bit BMP, every byte contains 2 pixels
												// 4 bytes contain 8 pixels
												uint8_t p0 = fgetc(file); uint8_t p1 = fgetc(file);
												uint8_t p2 = fgetc(file); uint8_t p3 = fgetc(file);
												_plane0 = ((p0&0x10)<<3)|((p0&0x01)<<6)|((p1&0x10)<<1)|((p1&0x01)<<4)|
												((p2&0x10)>>1)|((p2&0x01)<<2)|((p3&0x10)>>3)|(p3&0x01);
												_plane1 = ((p0&0x20)<<2)|((p0&0x02)<<5)|(p1&0x20   )|((p1&0x02)<<3)|
												((p2&0x20)>>2)|((p2&0x02)<<1)|((p3&0x20)>>4)|((p3&0x02)>>1);
												_plane2 = ((p0&0x40)<<1)|((p0&0x04)<<4)|((p1&0x40)>>1)|((p1&0x04)<<2)|
												((p2&0x40)>>3)|(p2&0x04)|((p3&0x40)>>5)|((p3&0x04)>>2);
												_plane3 = (p0&0x80)|((p0&0x08)<<3)|((p1&0x80)>>2)|((p1&0x08)<<1)|
												((p2&0x80)>>4)|((p2&0x08)>>1)|((p3&0x80)>>6)|((p3&0x08)>>3);
												buffer[    0        + _offset] = _plane0;
												buffer[plane_size   + _offset] = _plane1;
												buffer[plane_size*2 + _offset] = _plane2;
												buffer[plane_size*3 + _offset] = _plane3;
												_offset++;
											}
											fseek(file,-(width/2)-8, SEEK_CUR);//start of next scanline
										}
									}
								}
							}
							//sprite 32x32
							else if (width == 256 && height != 208) {
								uint8_t key = 0;
								int16_t jump_back = 0;
								int check_palette = 0;
								uint32_t _offset = 0;
								unsigned char cell = 0;
								uint32_t offset = 0;
								uint32_t offset1 = 0;
								printf("SPRITE %s; ",name);
								image_type = 3;
								buffer[_offset++] = 19;
								buffer[_offset++] = (width/32)*(height/32);//number of frames
								filesize+=2;

								offset = (width/2)*(height-1);
								for (index = 0; index < height; index++){
									fread(&temp_pixels[offset],1,width/2, file);
									offset-=(width/2);
								}
								offset= 0;
								for (index = 0; index < width/2*height; index++){
									unsigned char c = temp_pixels[index];
									final_pixels[offset  ] = (c >>4);
									final_pixels[offset+1] = (c & 0x0F);
									offset+=2;
								}
								////////////////
								//parse frames//
								////////////////
								transparent_sprite_color = final_pixels[0];
								printf("TRANSPARENT = %i\n",transparent_sprite_color);
								for (int y0 = 0; y0 < height; y0+=32){
									for (int x0 = 0; x0 < width; x0+= 32){
										uint32_t start_frame = _offset;
										_offset+=2;//skip sprite size
										filesize+=2;
										check_palette = 0;
										memset(sprite_palette,0,16);
										offset = ( y0 * width)+x0;
										offset1 = 0;
										//get used colors
										for (int y = 0; y < 32; y++){
											for (int x = 0; x < 32; x++){
												int found_new_color = 1;
												unsigned char pixel = final_pixels[offset++];
												if (pixel == transparent_sprite_color) continue;
												for (int i = 0; i < 16; i++){
													if (pixel == sprite_palette[i]) found_new_color = 0;
												}
												if (found_new_color) {
													if (check_palette > 15) {printf("PAL colors > 16)");return 0;}
													sprite_palette[1+check_palette] = pixel;
													check_palette++;
												}
											}
											offset+=width-32;
										}

										/////////////////////
										//create frame code//
										/////////////////////
										sprite_size = 0;
										//parse all colors
										for (int colors = 0; colors < check_palette+1; colors++){
											offset = ( y0 * width)+x0;
											//printf("mov al,0x%02X; out dx,al;\n",sprite_palette[colors]);
											buffer[_offset++]=0xB0;buffer[_offset++] = sprite_palette[colors];
											buffer[_offset++]=0xEE;
											sprite_size+=3;
											for (int y = 0; y < 32; y++){
												for (int x = 0; x < 4; x++){
													uint8_t mask = 0;
													uint16_t offset_vram = (y*50)+x;
													for (int p = 7; p > -1; p--){
														unsigned char pixel = final_pixels[offset++];
														if (pixel == transparent_sprite_color) continue;
														if (pixel != sprite_palette[colors]) continue;
														else mask |= 1<<p;
													}
													if (mask) {
														if (offset_vram > 127) {
															buffer[_offset++]=0x26;buffer[_offset++]=0x80;buffer[_offset++]=0xA5;
															buffer[_offset++]=offset_vram&0x00FF;
															buffer[_offset++]=offset_vram>>8;
															buffer[_offset++]=mask;
															sprite_size+=6;
															//printf("and byte ptr es:[di+0x%04X],0x%02x\n",offset_vram,mask);
														} else {
															buffer[_offset++]=0x26;buffer[_offset++]=0x80;buffer[_offset++]=0x65;
															buffer[_offset++]=offset_vram&0x00FF;
															buffer[_offset++]=mask;
															sprite_size+=5;
															//printf("and byte ptr es:[di+0x%02X],0x%02x\n",(uint8_t)offset_vram,mask);
														}
													}
												}
												offset+=width-32;
											}
										}

										//set right mask
										//printf("SET RIGHT MASK\n");
										buffer[_offset++]=0x4A;buffer[_offset++]=0x49;	// dec dx; dec cx;
										buffer[_offset++]=0x74;buffer[_offset++]=13;	// jz _end; skip next 13 bytes, to CB
										buffer[_offset++]=0x47;							// inc di;
										buffer[_offset++]=0xB0;buffer[_offset++]=0x08;	// mov al,0x08;
										buffer[_offset++]=0x88;buffer[_offset++]=0xFC;	// mov ah,bh;
										buffer[_offset++]=0xEF;							// out dx,ax; (bh right mask)
										buffer[_offset++]=0xB0;buffer[_offset++]=0x00;	// mov al,0x00;
										buffer[_offset++]=0xEE;buffer[_offset++]=0x42;	// out dx,al; inc dx; select Set/Reset reg
										//loop
										//printf("LOOP\n");
										sprite_size+=(14+3); jump_back = -sprite_size;//+3 to add jmp int16
										//jmp -int16_t to start;
										buffer[_offset++]=0xE9;
										buffer[_offset++]=jump_back&0x00FF; buffer[_offset++]=jump_back>>8;
										//Insert far return for every frame
										buffer[_offset++]=0xCB;
										sprite_size+=1;

										//printf("%i\n",sprite_size);
										buffer[start_frame] = sprite_size&0x00FF;
										buffer[start_frame+1] = sprite_size>>8;
										filesize+=sprite_size;
									}
								}
							}
							//sprite 128x128
							else if (width == 128 && height == 128) {
								uint8_t key = 0;
								int16_t jump_back = 0;
								int check_palette = 0;
								uint32_t _offset = 0;
								unsigned char cell = 0;
								uint32_t offset = 0;
								uint32_t offset1 = 0;
								printf("SPRITE 64 %s; ",name);
								image_type = 3;
								buffer[_offset++] = 15;
								buffer[_offset++] = (width/64)*(height/64);//number of frames
								filesize+=2;

								offset = (width/2)*(height-1);
								for (index = 0; index < height; index++){
									fread(&temp_pixels[offset],1,width/2, file);
									offset-=(width/2);
								}
								offset= 0;
								for (index = 0; index < width/2*height; index++){
									unsigned char c = temp_pixels[index];
									final_pixels[offset  ] = (c >>4);
									final_pixels[offset+1] = (c & 0x0F);
									offset+=2;
								}
								////////////////
								//parse frames//
								////////////////
								transparent_sprite_color = final_pixels[0];
								printf("TRANSPARENT = %i\n",transparent_sprite_color);
								for (int y0 = 0; y0 < height; y0+=64){
									for (int x0 = 0; x0 < width; x0+= 64){
										uint32_t start_frame = _offset;
										_offset+=2;//skip sprite size
										filesize+=2;
										check_palette = 0;
										memset(sprite_palette,0,16);
										offset = (y0 * width)+x0;
										offset1 = 0;
										//get used colors
										for (int y = 0; y < 64; y++){
											for (int x = 0; x < 64; x++){
												int found_new_color = 1;
												unsigned char pixel = final_pixels[offset++];
												if (pixel == transparent_sprite_color) continue;
												for (int i = 0; i < 16; i++){
													if (pixel == sprite_palette[i]) found_new_color = 0;
												}
												if (found_new_color) {
													if (check_palette > 15) {printf("PAL colors > 16)");return 0;}
													sprite_palette[1+check_palette] = pixel;
													check_palette++;
												}
											}
											offset+=width-64;
										}

										/////////////////////
										//create frame code//
										/////////////////////
										sprite_size = 0;
										//parse all colors
										for (int colors = 0; colors < check_palette+1; colors++){
											offset = (y0 * width)+x0;
											//printf("mov al,0x%02X; out dx,al;\n",sprite_palette[colors]);
											buffer[_offset++]=0xB0;buffer[_offset++] = sprite_palette[colors];
											buffer[_offset++]=0xEE;
											sprite_size+=3;
											for (int y = 0; y < 64; y++){
												for (int x = 0; x < 8; x++){
													uint8_t mask = 0;
													uint16_t offset_vram = (y*50)+x;
													for (int p = 7; p > -1; p--){
														unsigned char pixel = final_pixels[offset++];
														if (pixel == transparent_sprite_color) continue;
														if (pixel != sprite_palette[colors]) continue;
														else mask |= 1<<p;
													}
													if (mask) {
														if (offset_vram > 127) {
															buffer[_offset++]=0x26;buffer[_offset++]=0x80;buffer[_offset++]=0xA5;
															buffer[_offset++]=offset_vram&0x00FF;
															buffer[_offset++]=offset_vram>>8;
															buffer[_offset++]=mask;
															sprite_size+=6;
															//printf("and byte ptr es:[di+0x%04X],0x%02x\n",offset_vram,mask);
														} else {
															buffer[_offset++]=0x26;buffer[_offset++]=0x80;buffer[_offset++]=0x65;
															buffer[_offset++]=offset_vram&0x00FF;
															buffer[_offset++]=mask;
															sprite_size+=5;
															//printf("and byte ptr es:[di+0x%02X],0x%02x\n",(uint8_t)offset_vram,mask);
														}
													}
												}
												offset+=width-64;
											}
										}

										//set right mask
										//printf("SET RIGHT MASK\n");
										buffer[_offset++]=0x4A;buffer[_offset++]=0x49;	// dec dx; dec cx;
										buffer[_offset++]=0x74;buffer[_offset++]=13;	// jz _end; skip next 13 bytes, to CB
										buffer[_offset++]=0x47;							// inc di;
										buffer[_offset++]=0xB0;buffer[_offset++]=0x08;	// mov al,0x08;
										buffer[_offset++]=0x88;buffer[_offset++]=0xFC;	// mov ah,bh;
										buffer[_offset++]=0xEF;							// out dx,ax; (bh right mask)
										buffer[_offset++]=0xB0;buffer[_offset++]=0x00;	// mov al,0x00;
										buffer[_offset++]=0xEE;buffer[_offset++]=0x42;	// out dx,al; inc dx; select Set/Reset reg
										//loop
										//printf("LOOP\n");
										sprite_size+=(14+3); jump_back = -sprite_size;//+3 to add jmp int16
										//jmp -int16_t to start;
										buffer[_offset++]=0xE9;
										buffer[_offset++]=jump_back&0x00FF; buffer[_offset++]=jump_back>>8;
										//Insert far return for every frame
										buffer[_offset++]=0xCB;
										sprite_size+=1;

										//printf("%i\n",sprite_size);
										buffer[start_frame] = sprite_size&0x00FF;
										buffer[start_frame+1] = sprite_size>>8;
										filesize+=sprite_size;
									}
								}
							}
							else {
								printf("Image must be / Imagen debe ser:\n");
								printf(" -ANIMATION 512x384\n");
								printf(" -IMAGE 320x240\n");
								printf(" -TILES 512x384\n");
								printf(" -SPRITE (32x32) 256x128\n");
								printf(" -SPRITE (64x64) 128x128\n");
								return 0;
							}
							break;
							case 3://VGM or MOD
								fseek(file, 0, SEEK_END);
								filesize = ftell(file);
								rewind(file);
								fread(buffer,filesize,1,file);
								break;
							case 2://store tmx maps more efficiently
								while(!start_map_data){	//read lines
									memset(line, 0, 64);
									fgets(line, 64, file);
									// <tileset firstgid="number" name... two lines like this
									// last one is first collision tile index, we will subtract it
									if((line[1] == '<')&&(line[2] == 't')) {
										int n = sscanf(line, " <tileset firstgid=\"%d\" name", &tilecount);
										if (!n) {printf("Not a tiled 1.1 map"); return 0;}
									}
									//  <data encoding="csv">
									if((line[2] == '<')&&(line[3] == 'd')) start_map_data = 1;
								}
								start_map_data = 0;
								filesize = 0;
								//fseek(file, 0, SEEK_SET);
								//read map in bytes
								for (index = 0; index < 512*36; index++){
									int tile = 0;
									uint16_t *buf = (uint16_t*) &buffer[filesize];
									fscanf(file, "%d,",&tile); if (tile) tile--; // base 0
									buf[0] = TILE_VRAM + (uint16_t)((tile)<<5);
									//printf("%i,",tile);
									filesize+=2;
								}
								//advance to bkg map
								while(!start_map_data){	//read lines
									memset(line, 0, 64);
									fgets(line, 64, file);
									if((line[2] == '<')&&(line[3] == 'd')) start_map_data = 1;//<data encoding="csv">
								}
								//read bkg map in bytes
								for (index = 0; index < 512*36; index++){
									int tile = 0;
									fscanf(file, "%d,",&tile);
									tile-=tilecount; //subtract, leave base 1, collision 0 not used
									buffer[filesize++] = (unsigned char)tile;
								}
								break;
					}

					//Write file offset in data, and file size
					file_pos = dat_offset;
					fseek(dat, (nfiles*32)+16, SEEK_SET);
					fwrite(&file_pos,4,1,dat);
					fwrite(&filesize,4,1,dat);
					//Write actual file
					fseek(dat,file_pos, SEEK_SET);
					fwrite(buffer,filesize,1,dat);
					dat_offset += filesize;

					fclose(file);
					nfiles++;
				}
			}
		}
		fclose(dat);
	
	free(buffer);
	buffer = NULL;
    return 0;
}
