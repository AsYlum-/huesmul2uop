#include "uohues.h"
#include <iostream>
#include <fstream>
#include <cmath>


ARGB32 convert_ARGB16_to_ARGB32(ARGB16 argb16, bool maxOpacity)
{
    // In ARGB16 each color is 5 bits, so a value between 0-31
    uint8_t a = argb16.getA();
    uint8_t r = argb16.getR();
    uint8_t g = argb16.getG();
    uint8_t b = argb16.getB();

    // In ARGB32 each color is 8 bits, so a value between 0-255
    // Convert them to 0-255 range:
    /*
    There is more than one way. You can just shift them left:
    to 00000000 rrrrr000 gggggg00 bbbbb000
    r <<= 3;
    g <<= 2;
    b <<= 3;
    But that means your image will be slightly dark and
    off-colour as white 0xFFFF will convert to F8,FC,F8
    So instead you can scale by multiply and divide:
*/

    // More exact conversion
    // This ensures 31/31 converts to 255/255
    //r = (uint8_t)(r * 255 / 31);        // R
    //g = (uint8_t)(g * 255 / 31);        // G
    //b = (uint8_t)(b * 255 / 31);        // B

    // Conversion from (AxisII)
    // Image slightly darker (probably though it looks more like the img shown in the uo client)
    r *= 8;        // R
    g *= 8;        // G
    b *= 8;        // B

    // If the pixel is black, set it to be full transparent
    if (maxOpacity)
        a = 255;
    //else if ( !r && !g && !b )
    //    a = 0;  // max transparency

    return ARGB32(a, r, g, b);
}

ARGB32 convert_ARGB16_to_ARGB32_exact(ARGB16 argb16, bool maxOpacity)
{
    uint8_t a = argb16.getA();
    uint8_t r = argb16.getR();
    uint8_t g = argb16.getG();
    uint8_t b = argb16.getB();

    // More exact conversion
    // This ensures 31/31 converts to 255/255
    r = uint8_t(r * 255 / 31);        // R
    g = uint8_t(g * 255 / 31);        // G
    b = uint8_t(b * 255 / 31);        // B

    // If the pixel is black, set it to be full transparent
    if (maxOpacity)
        a = 255;
    //else if ( !r && !g && !b )
    //    a = 0;  // max transparency

    return ARGB32(a, r, g, b);
}

void ARGB32::adjustBrightness(int percent)
{
    int tempR = m_color_r + ((m_color_r * percent) / 100);
    if (tempR > 255)    tempR = 255;
    else if (tempR < 0) tempR = 0;
    m_color_r = uint8_t(tempR);

    int tempG = m_color_g + ((m_color_g * percent) / 100);
    if (tempG > 255)    tempG = 255;
    else if (tempG < 0) tempG = 0;
    m_color_g = uint8_t(tempG);

    int tempB = m_color_b + ((m_color_r * percent) / 100);
    if (tempB > 255)    tempB = 255;
    else if (tempB < 0) tempB = 0;
    m_color_b = uint8_t(tempB);
}


/*
void ARGB32::adjustSaturation(int percent)
{
    // credits: http://alienryderflex.com/saturation.html
    // Am i doing something wrong or this doesn't work correctly?
    double r = m_color_r;
    double g = m_color_g;
    double b = m_color_b;

    static const double Pr = .299;
    static const double Pg = .587;
    static const double Pb = .114;
    double P = sqrt( (r*r * Pr) + (g*g * Pg) +(b*b * Pb) );

    double change = (double)percent / 100.f;
    m_color_r = (uint8_t)(P+(r-P)*change);
    m_color_g = (uint8_t)(P+(g-P)*change);
    m_color_b = (uint8_t)(P+(b-P)*change);
}
*/

UOHues::UOHues(std::string path_hues)
{
    std::ifstream fs_hues;
    fs_hues.open(path_hues, std::ifstream::in | std::ifstream::binary);
    if (!fs_hues.is_open())
    {
        std::cout << "Error opening hues.mul." << std::endl;
        return;
    }

    /*
    HueGroup
    --------
    DWORD Header;
    HueEntry Entries[8];

    HueEntry
    --------
    WORD ColorTable[32];
    WORD TableStart;
    WORD TableEnd;
    CHAR Name[20];
    */
    // Just read in HueGroups until you hit the end of the file.
    unsigned int hue_num = 0;
    while (!fs_hues.eof() && !fs_hues.fail())
    {
        fs_hues.seekg(4, std::ifstream::cur);   // Skip Header of the hue group
        for (unsigned int i=0 ; i < 8 ; ++i)    // Iterate through the 8 HueEntries of this HueGroup
        {
            if (hue_num >= sizeof(hues))
                break;

            //hues[hue_num].index = hue_num;
            // Fill the ColorTable
            fs_hues.read(reinterpret_cast<char*>(&hues[hue_num].color_table), sizeof(hues[hue_num].color_table));
            //for (unsigned int j = 0; j < sizeof(hues[hue_num].color_table); ++j)
            //    hues[hue_num].color_table[j] |= 0x8000;
            // Skip TableStart and TableEnd
            fs_hues.seekg(4, std::ifstream::cur);
            // Read the Hue Name
            //memset(hues[hue_num].name, 0, sizeof(hues[hue_num].name));
            fs_hues.read(reinterpret_cast<char*>(&hues[hue_num].name), sizeof(hues[hue_num].name));
            ++hue_num;
        }
    }
}

/*
UOHues::~UOHues()
{

}
*/

UOHueEntry UOHues::getHueEntry(int index) const
{
    index &= 0x3FFF;

    // for the client: 0 is no hue
    // for hues.mul: 0 is the first entry
    // so the client starts to count from 1
    index -= 1;
    if (index >= 0 && index < 3000)
        return hues[index];
    else
        return hues[0];
}

ARGB16 UOHueEntry::getColor(unsigned int index) const
{
    if (index >= 32)
        return ARGB16(0);
    return ARGB16(color_table[index]);
}

std::string UOHueEntry::getName() const
{
    return std::string(name);
}

ARGB16 UOHueEntry::applyToColor(ARGB16 color16, bool applyToGrayOnly)
{
    /*
      Hues are applied to an image in one of two ways. Either all gray pixels are mapped
      to the specified color range (resulting in a part of an image changed to that color)
      or all pixels are first mapped to grayscale and then the specified color range is mapped over the entire image.
    */

    // If it's full transparent, do i have to change the color?
    //if (color16.getA() == 0)
    //    return color16;

    if (applyToGrayOnly)
    {
        if (color16.getA() == 0)
            return color16;
        else if ( !((color16.getR() == color16.getG()) && (color16.getR() == color16.getB())) )
            return color16;
    }

    /*  -- The AxisII Way -- */         // Dunno why, this way the image looks bad
    // Grayscale the input color
    //unsigned int ret = color16.getR() + color16.getG() + color16.getB();
    //ret /= 3;   // will always be < 32

    //return ARGB16(color_table[ret]);

    /* -- UOFiddler/Punt's Way -- */
    return ARGB16(color_table[color16.getR()]);   // ret will always be < 32
}

