// pph asm xdiff 17458
// Looks like destructors botched.

#include "x0keyed1.h"

int keyed::key( int arg ) { return mix( field & arg ); }

int main()
{
    keyed variable;
    return variable.key( 3 );
}
