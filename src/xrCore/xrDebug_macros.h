#ifndef xrDebug_macrosH
#define xrDebug_macrosH
#pragma once

//#define ANONYMOUS_BUILD

#ifndef __BORLANDC__
#	ifndef ANONYMOUS_BUILD
#		define DEBUG_INFO				__FILE__,__LINE__,__FUNCTION__
#	else // ANONYMOUS_BUILD
#		define DEBUG_INFO				"",__LINE__,""
#	endif // ANONYMOUS_BUILD
#else // __BORLANDC__
#	define DEBUG_INFO					__FILE__,__LINE__,__FILE__
#endif // __BORLANDC__

#ifdef ANONYMOUS_BUILD
	#define _TRE(arg)	""
#else
	#define _TRE(arg)	arg
#endif


#define CHECK_OR_EXIT(expr,message)	if (!(expr)) ::Debug.do_exit(message)

#define R_ASSERT(expr)				if (!(expr)) ::Debug.fail(_TRE(#expr),DEBUG_INFO,false)
#define R_ASSERT2(expr,e2)			if (!(expr)) ::Debug.fail(_TRE(#expr),_TRE(e2),DEBUG_INFO,false)
#define R_ASSERT3(expr,e2,e3)		if (!(expr)) ::Debug.fail(_TRE(#expr),_TRE(e2),_TRE(e3),DEBUG_INFO,false)
#define R_ASSERT4(expr,e2,e3,e4)		if (!(expr)) ::Debug.fail(_TRE(#expr),_TRE(e2),_TRE(e3),_TRE(e4),DEBUG_INFO,false)
#define R_CHK(expr)					if (FAILED(expr)) ::Debug.error(expr,_TRE(#expr),DEBUG_INFO,false)
#define R_CHK2(expr,e2)				if (FAILED(expr)) ::Debug.error(expr,_TRE(#expr),_TRE(e2),DEBUG_INFO,false)
#define FATAL(description)			Debug.fatal(DEBUG_INFO,description)

#	ifdef VERIFY
#		undef VERIFY
#	endif // VERIFY


#ifdef __BORLANDC__
#	define NODEFAULT
#else
#	define NODEFAULT __assume(0)
#endif
#define VERIFY(expr)
#define VERIFY2(expr, e2)
#define VERIFY3(expr, e2, e3)
#define VERIFY4(expr, e2, e3, e4)
#define CHK_DX(a) a
//---------------------------------------------------------------------------------------------
// FIXMEs / TODOs / NOTE macros
//---------------------------------------------------------------------------------------------
#define _QUOTE(x) # x
#define QUOTE(x) _QUOTE(x)
#define __FILE__LINE__ __FILE__ "(" QUOTE(__LINE__) ") : "

#define NOTE( x )  message( x )
#define FILE_LINE  message( __FILE__LINE__ )

#define TODO( x )  message( __FILE__LINE__"\n"           \
	" ------------------------------------------------\n" \
	"|  TODO :   " #x "\n" \
	" -------------------------------------------------\n" )
#define FIXME( x )  message(  __FILE__LINE__"\n"           \
	" ------------------------------------------------\n" \
	"|  FIXME :  " #x "\n" \
	" -------------------------------------------------\n" )
#define todo( x )  message( __FILE__LINE__" TODO :   " #x "\n" ) 
#define fixme( x )  message( __FILE__LINE__" FIXME:   " #x "\n" ) 

//--------- static assertion
template<bool>	struct CompileTimeError;
template<>		struct CompileTimeError<true>	{};
#define STATIC_CHECK(expr, msg) \
{ \
	CompileTimeError<((expr) != 0)> ERROR_##msg; \
	(void)ERROR_##msg; \
}
#endif // xrDebug_macrosH