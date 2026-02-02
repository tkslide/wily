#include	<tcl.h>
#include	<tk.h>
#include	"wtcl.h"

extern int
Tcl_AppInit(
	Tcl_Interp	*interp
)
{
	if(Tcl_Init(interp) == TCL_ERROR)
		return TCL_ERROR ;

	if(Tk_Init(interp) == TCL_ERROR)
		return TCL_ERROR ;

	if(Wily_Init(interp) == TCL_ERROR)
		return TCL_ERROR ;

	Tcl_SetVar(interp, "tcl_rcFileName", "~/.wtk", TCL_GLOBAL_ONLY) ;

	return TCL_OK ;
}

extern int
main(
	int		argc,
	char	**argv
)
{
	Tk_Main(argc, argv, Tcl_AppInit) ;
	return 0 ;
}

