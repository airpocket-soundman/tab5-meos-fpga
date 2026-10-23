#include <assert.h>
#include <string.h>
#include "../../user/bluetooth/bt_app.h"
static bool seen;
static void output(const char *text) {seen=strstr(text,"not built")!=NULL;}
int main(void) {char *args[]={"bt","on"};assert(bt_app_command(2,args,output)==1&&seen);}
