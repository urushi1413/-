// SWEET SPOT — 保存するファイルを、ブラウザ版ではブラウザの保存場所（localStorage）にも写す
//   ブラウザ版のファイルはメモリの中（MEMFS）なので、ページを閉じると消える。読む前に localStorage から戻し、書いた後に写す
#include "game.h"
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

// 読む前に呼ぶ：ブラウザ版なら localStorage の中身をファイルに書き戻す（パソコン版は何もしない）
void StoreRestore(const char *file)
{
#ifdef __EMSCRIPTEN__
    EM_ASM({ try { var k = 'sweetspot_' + UTF8ToString($0); var v = localStorage.getItem(k);
                   if (v !== null) FS.writeFile(UTF8ToString($0), v); } catch (e) {} }, file);
#else
    (void)file;
#endif
}

// 書いて閉じた後に呼ぶ：ブラウザ版ならファイルの中身を localStorage に写す
void StorePersist(const char *file)
{
#ifdef __EMSCRIPTEN__
    EM_ASM({ try { var f = UTF8ToString($0);
                   localStorage.setItem('sweetspot_' + f, FS.readFile(f, { encoding: 'utf8' })); } catch (e) {} }, file);
#else
    (void)file;
#endif
}
