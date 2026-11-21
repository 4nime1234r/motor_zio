#pragma once

namespace zio_editor
{

struct IconBitmap
{
    const char* name;
    const unsigned char* alpha;
};

constexpr int kIconBitmapSize = 32;

const IconBitmap* iconBitmaps(int& count);

}
