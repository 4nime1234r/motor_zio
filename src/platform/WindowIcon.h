#pragma once

#include <windows.h>

namespace zio::platform
{

inline void loadWindowIcons(HINSTANCE instance, HICON& large, HICON& small)
{
    constexpr WORD kIconResourceId = 1;
    large = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(kIconResourceId), IMAGE_ICON,
                                          GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_SHARED));
    small = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(kIconResourceId), IMAGE_ICON,
                                          GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED));
    if (!large)
        large = LoadIconW(nullptr, IDI_APPLICATION);
    if (!small)
        small = large;
}

}
