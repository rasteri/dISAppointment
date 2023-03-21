/*
Sapphisa
DOS utility to configure Fintek F85226 LPC-ISA bridges
and motherboard chipsets

Usage : 
	sapphisa [base1 mask1] [base2 mask2] [base3 mask3] [base4 mask4]

Check the F85226 datasheet for information on how the masks work.

By default it forwards the following ports : 
200-2FF -- Soundblaster DSP
300/310/320/330/340/350/360/370 (+1/2/3) -- MPU-401 MIDI
380-38F, 390-39F -- Adlib
A00-AFF -- PnP

To compile (requires DJGPP) : 
	gcc sapphisa.c -o sapphisa.exe	
*/

#include <pc.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/nearptr.h>
#include <dpmi.h>
#include <malloc.h>

#define uint32_t unsigned long
#define uint16_t unsigned short
#define uint8_t unsigned char

void listports(uint32_t BASE, uint32_t MASK)
{
    uint32_t cnt;
    uint32_t lastOne = 0;
    uint32_t thisOne = 0;
    //each bit in the mask is ignored in the address
    MASK |= 0x03;
    for (cnt = 0; cnt < 0xFFFF; cnt++)
    {
        if ((cnt | MASK) == (BASE | MASK))
        {
            // starting new run, print first
            if (lastOne == 0){
                printf("%X-", cnt);
                lastOne = cnt;
            }
            else if (cnt == lastOne + 1)
            {
                // don't print consecutive ports
                lastOne = cnt;
            }
            else // we've broken a sequence, print the last one and start a new range
            {
                printf("%X,", lastOne);
                printf("%X-", cnt);
                lastOne = cnt;
            }
        }
    }
    printf("%X", lastOne);
    printf("\n");
    printf("\n");
}

void dec(uint32_t val)
{
    uint32_t BASE = (val & 0xFFFC);
    uint32_t MASK = (val & 0xFC0000) >> 16;
    printf("BASE : %lx\n", BASE);
    printf("MASK : %lx\n", MASK);

    listports(BASE, MASK);
}

uint32_t LPCEnc(uint32_t BASE, uint32_t MASK)
{
    return BASE | (MASK << 16) | 0x01;
}

typedef struct range
{
    // base address, only LS 14 bits are used
    uint32_t base;

    // only MS 6 bits are used
    // any digits that are 1 will be ignored in the decode comparison logic
    uint32_t mask;
} range;

uint32_t LPCGenericDecode[] = {0x8000F884, 0x8000F888, 0x8000F88C, 0x8000F890};

uint32_t ISAGenericDecode[] = {0x20, 0x23, 0x30, 0x33};

// default ranges
range ranges[] = {
    {0x200, 0xFC}, //200-2FF
    {0x300, 0x70}, //300/310/320/330/340/350/360/370+3
    {0x388, 0x1C}, //380-38F, 390-39F
    {0xA00, 0xFC}  //A00-AFF
};

void writepci( uint32_t address, uint32_t val) {
	outportl(0xCF8, address);
	outportb(0xCFC, val & 0xFF);
	val >>= 8;
	outportb(0xCFD, val & 0xFF);
	val >>= 8;
	outportb(0xCFE, val & 0xFF);
	val >>= 8;
	outportb(0xCFF, val & 0xFF);
	outportl(0xCF8, 0x00000000);
}

uint32_t readpci( uint32_t address) {
	uint32_t val;
	outportl(0xCF8, address);
	val = inportl(0xCFC);
	outportl(0xCF8, 0x00000000);
    return val;
}

int main(int argc, char *argv[])
{
    uint32_t tmp;
    int numRanges;
    if (argc < 3 || !(argc & 0x01))
    {
        printf("Malformed args, using defaults\n");
        numRanges = 4;
    }
    else
    {
        numRanges = (argc - 1) / 2;
        for (tmp = 0; tmp < numRanges; tmp++)
        {
            ranges[tmp].base = strtoul(argv[(tmp * 2) + 1], NULL, 16);
            ranges[tmp].mask = strtoul(argv[(tmp * 2) + 2], NULL, 16);
        }
    }

    // Check the LPC controller is there
    if (readpci(0x8000F800) & 0x0000FFFF != 0x00008086)
    {
        printf("Can't find Intel LPC controller.\n");
        printf("(Got %X, expected %X)\n", tmp, 0x8086);
        goto errexit;
    }
    printf("Found Intel LPC Controller.\n");

	// forward addresses 4E and 4F (Fintek ISA bridge config port) to the LPC bus
    writepci( 0x8000F880, readpci(0x8000F880) | 0x20000000);

    // Enable ISA bridge config registers
    outportb(0x4E, 0x26);
    outportb(0x4E, 0x26);

    // check the ISA bridge is there
    tmp = 0;
    outportb(0x4E, 0x5A);
    tmp |= inportb(0x4F) << 24;
    outportb(0x4E, 0x5B);
    tmp |= inportb(0x4F) << 16;
    outportb(0x4E, 0x5C); 
	//tmp |= inportb(0x4F);
    outportb(0x4E, 0x5D);
    tmp |= inportb(0x4F) << 8;
    outportb(0x4E, 0x5E);
    tmp |= inportb(0x4F);

    if (tmp != 0x03051934)
    {
        printf("Can't find Fintek F85226FG LPC-ISA Bridge\n");
        printf("(Got %X, expected %X)\n", tmp, 0x03051934);
        goto errexit;
    }
    printf("Found Fintek F85226FG LPC-ISA Bridge\n");

	// forward the address ranges to the ISA bridge
    for (tmp = 0; tmp < numRanges; tmp++)
    {
        printf("Enabling Range %x : Base %x, Mask %x, LPC %x\n", tmp, ranges[tmp].base, ranges[tmp].mask, LPCEnc(ranges[tmp].base, ranges[tmp].mask));
        printf ("Ports : ");
        listports(ranges[tmp].base, ranges[tmp].mask);

        // first forward the port in the LPC controller
		writepci(LPCGenericDecode[tmp], LPCEnc(ranges[tmp].base, ranges[tmp].mask));

        // now tell the ISA Bridge to expect it
        outportb(0x4E, ISAGenericDecode[tmp] + 1);
        outportb(0x4F, ranges[tmp].base & 0xFF);

        outportb(0x4E, ISAGenericDecode[tmp] + 2);
        outportb(0x4F, (ranges[tmp].base >> 8) & 0xFF);

        outportb(0x4E, ISAGenericDecode[tmp]);
        outportb(0x4F, ranges[tmp].mask | 0x03);
    }

    // Enable A17/A18/A19
    outportb(0x4E, 0x05);
    outportb(0x4F, 0x0E);

    // 8MHz clock, 3 8bit waitstates, 1 16bit waitstate
    outportb(0x4E, 0x06);
    outportb(0x4F, 0b01011101);

    // Output sysclk
    outportb(0x4E, 0x50);
    outportb(0x4F, 0x00);

    // Disable power management
    outportb(0x4E, 0x51);
    outportb(0x4F, 0x00);
    outportb(0x4E, 0x51);
    outportb(0x4F, 0x00);

	// Reset some DMA registers because the BIOS doesn't do this
	// Not sure how much of this is actually required but hey
	outportb(0x00, 0x00);
	outportb(0x01, 0x00);
	outportb(0x04, 0x00);
	outportb(0x05, 0x00);
	outportb(0x06, 0x00);
	outportb(0x07, 0x00);
	outportb(0x08, 0x00);
	outportb(0x21, 0x00);
	outportb(0x82, 0x00);
	outportb(0x87, 0x00);
	outportb(0x89, 0x00);
	outportb(0x8a, 0x00);
	outportb(0x8b, 0x00);
	outportb(0xc0, 0x00);
	outportb(0xc1, 0x00);
	outportb(0xc2, 0x00);
	outportb(0xc3, 0x00);
	outportb(0xc4, 0x00);
	outportb(0xc5, 0x00);
	outportb(0xc6, 0x00);
	outportb(0xc7, 0x00);
	outportb(0xc8, 0x00);
	outportb(0xc9, 0x00);
	outportb(0xca, 0x00);
	outportb(0xcb, 0x00);
	outportb(0xcc, 0x00);
	outportb(0xcd, 0x00);
	outportb(0xce, 0x00);
	outportb(0xcf, 0x00);
	outportb(0xd0, 0x00);
	outportb(0xd1, 0x00);
	outportb(0xde, 0x0e);
	outportb(0xdf, 0x0e);

    return 0;

errexit:
    outportl(0xCF8, 0x00);
    return 1;
}
