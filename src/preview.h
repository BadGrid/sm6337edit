#ifndef PREVIEW_H
#define PREVIEW_H
#include <windows.h>
#include "printer.h"

/* Показывает предпросмотр по готовому потоку ESC/P (тому же, что уйдёт на LPT1). */
void Sm6337_ShowPreview(HWND owner, HINSTANCE hInst, const unsigned char *job, DWORD len, const Sm6337Settings *s);

#endif
