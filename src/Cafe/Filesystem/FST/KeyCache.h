#pragma once

void KeyCache_Prepare();

uint8* KeyCache_GetAES128(sint32 index);

#if BOOST_PLAT_ANDROID
sint32 KeyCache_Reload();
#endif