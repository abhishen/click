// -*- c-basic-offset: 4; related-file-name: "../include/click/element.hh" -*-
/*
 * element.{cc,hh} -- the Element base class
 * Eddie Kohler
 * statistics: Robert Morris
 *
 * Copyright (c) 1999-2000 Massachusetts Institute of Technology
 * Copyright (c) 2004-2005 Regents of the University of California
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software")
 * to deal in the Software without restriction, subject to the conditions
 * listed in the Click LICENSE file. These conditions include: you must
 * preserve this copyright notice, and you cannot mention the copyright
 * holders in advertising related to the Software without their permission.
 * The Software is provided WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED. This
 * notice is a summary of the Click LICENSE file; the license in that file is
 * legally binding.
 */

#include <click/config.h>

#include <click/glue.hh>
#include <click/element.hh>
#include <click/bitvector.hh>
#include <click/confparse.hh>
#include <click/error.hh>
#include <click/router.hh>
#include <click/master.hh>
#include <click/straccum.hh>
#if CLICK_LINUXMODULE
# include <click/cxxprotect.h>
CLICK_CXX_PROTECT
# include <asm/types.h>
# include <asm/uaccess.h>
CLICK_CXX_UNPROTECT
# include <click/cxxunprotect.h>
#endif
CLICK_DECLS

const char Element::PORTS_0_0[] = "0";
const char Element::PORTS_0_1[] = "0/1";
const char Element::PORTS_1_0[] = "1/0";
const char Element::PORTS_1_1[] = "1";

const char Element::AGNOSTIC[] = "a";
const char Element::PUSH[] = "h";
const char Element::PULL[] = "l";
const char Element::PUSH_TO_PULL[] = "h/l";
const char Element::PULL_TO_PUSH[] = "l/h";

const char Element::COMPLETE_FLOW[] = "x/x";

int Element::nelements_allocated = 0;

#if CLICK_STATS >= 2
# define ELEMENT_CTOR_STATS _calls(0), _self_cycles(0), _child_cycles(0),
#else
# define ELEMENT_CTOR_STATS
#endif

/** @class Element
 * @brief Base class for Click elements. */

/** @class Element::Port
 * @brief An Element's ports.
 *
 * Each of an element's ports has an associated Port object, accessible via
 * the Element::port(), Element::input(), and Element::output() functions.
 * Each @em active port knows, and can transfer a packet with, the single
 * complementary port to which it is connected.  Thus, each push output knows
 * its connected input, and can push a packet there; and each pull input can
 * pull a packet from its connected output.  Inactive ports -- push inputs and
 * pull outputs -- can be connected multiple times.  Their corresponding Port
 * objects have very little functionality.
 *
 * Element authors generally encounter Port objects only in two stylized
 * formulations.  First, to push a packet @c p out on push output port @c i:
 *
 * @code
 * output(i).push(p);
 * @endcode
 *
 * And second, to pull a packet @c p from pull input port @c i:
 *
 * @code
 * Packet *p = input(i).pull();
 * @endcode
 *
 * @sa Element::checked_output_push() */

/** @brief Constructs an Element. */
Element::Element()
    : ELEMENT_CTOR_STATS _router(0), _eindex(-1)
{
    nelements_allocated++;
    _ports[0] = _ports[1] = &_inline_ports[0];
    _nports[0] = _nports[1] = 0;
}

/** @brief Constructs an Element with @a ninputs input ports and @a noutputs
 * output ports (deprecated).
 *
 * @param ninputs number of input ports
 * @param noutputs number of output ports
 *
 * @deprecated This constructor is deprecated.  Elements should use the
 * port_count() function, not the constructor, to define the expected numbers
 * of ports.
 *
 * @sa port_count
 */
Element::Element(int ninputs, int noutputs)
    : ELEMENT_CTOR_STATS _router(0), _eindex(-1)
{
    nelements_allocated++;
    _ports[0] = _ports[1] = &_inline_ports[0];
    _nports[0] = _nports[1] = 0;
    set_nports(ninputs, noutputs);
}

Element::~Element()
{
    nelements_allocated--;
    if (_ports[1] != _inline_ports && _ports[1] != _inline_ports + _nports[0])
	delete[] _ports[1];
    if (_ports[0] != _inline_ports)
	delete[] _ports[0];
}

// CHARACTERISTICS

/** @fn Element::class_name() const
 * @brief Return the element's class name.
 *
 * Each element class must override this function to return its class name.
 *
 * Click tools extract class names from the source.  For Click to find a class
 * name, the function definition must appear inline, on a single line, inside
 * the element class's declaration, and must return a C string constant.  It
 * should also have public accessibility.  Here's an acceptable class_name()
 * definition:
 *
 * @code
 * const char *class_name() const     { return "ARPQuerier"; }
 * @endcode
 */

/** @brief Called to attempt to cast the element to a named type.
 *
 * @param name name of the type being cast to
 *
 * Click calls this function to see whether this element has a given type,
 * identified by @a name.  Thus, cast() is Click's version of the C++ @c
 * dynamic_cast operator.  (@c dynamic_cast itself is not available in the
 * Linux kernel, so we rolled our own.)  The function should return a pointer
 * to the named object, or a null pointer if this element doesn't have that
 * type.  @a name can name an element class or another type of interface, such
 * as @c "Storage" or Notifier::EMPTY_NOTIFIER.
 *
 * The default implementation returns this element if @a name equals
 * class_name(), and null otherwise.
 *
 * You should override cast() if your element inherits from another element
 * (and you want to expose that inheritance to Click); the resulting cast()
 * method will check both class names.  For example, if element @a B inherited
 * from element @a A, B::cast() might be defined like this:
 *
 * @code
 * void *B::cast(const char *name) {
 *     if (strcmp(name, "B") == 0)
 *         return (B *) this;
 *     else if (strcmp(name, "A") == 0)
 *         return (A *) this;
 *     else
 *         return A::cast(name);
 * }
 * @endcode
 *
 * The recursive call to A::cast() is useful in case @e A itself overrides
 * cast().  You should also override cast() if your element provides another
 * interface, such as Storage or a Notifier.
 */
void *
Element::cast(const char *name)
{
    const char *my_name = class_name();
    if (my_name && name && strcmp(my_name, name) == 0)
	return this;
    else
	return 0;
}

/** @brief Return the element's master. */
Master *
Element::master() const
{
    return _router->master();
}

/** @brief Return the element's name.
 *
 * This is the name used to declare the element in the router configuration,
 * with all compound elements expanded. */
String
Element::id() const
{
    String s;
    if (Router *r = router())
	s = r->ename(_eindex);
    return (s ? s : String::stable_string("<unknown>", 9));
}

/** @brief Return a string giving the element's name and class name.
 *
 * The result has the form &quot;@e name :: @e class_name&quot;.  Element
 * classes can override this function to supply additional important
 * information, if desired; for example, @e FromDump returns a string &quot;@e
 * name :: @e class_name(@e filename)&quot;.
 */
String
Element::declaration() const
{
    return id() + " :: " + class_name();
}

/** @brief Return a string describing where the element was declared.
 *
 * The string generally has the form
 * &quot;<em>filename</em>:<em>linenumber</em>&quot;. */
String
Element::landmark() const
{
    String s;
    if (Router *r = router())
	s = r->elandmark(_eindex);
    return (s ? s : String::stable_string("<unknown>", 9));
}

// INPUTS AND OUTPUTS

int
Element::set_nports(int new_ninputs, int new_noutputs)
{
    // exit on bad counts, or if already initialized
    if (new_ninputs < 0 || new_noutputs < 0)
	return -EINVAL;
    if (_router->_have_connections) {
	if (_router->_state >= Router::ROUTER_PREINITIALIZE)
	    return -EBUSY;
	_router->_have_connections = false;
    }
    
    // decide if inputs & outputs were inlined
    bool old_in_inline =
	(_ports[0] == _inline_ports);
    bool old_out_inline =
	(_ports[1] == _inline_ports || _ports[1] == _inline_ports + _nports[0]);

    // decide if inputs & outputs should be inlined
    bool new_in_inline =
	(new_ninputs == 0
	 || new_ninputs + new_noutputs <= INLINE_PORTS
	 || (new_ninputs <= INLINE_PORTS && new_noutputs > INLINE_PORTS)
	 || (new_ninputs <= INLINE_PORTS && new_ninputs > new_noutputs
	     && processing() == PULL));
    bool new_out_inline =
	(new_noutputs == 0
	 || new_ninputs + new_noutputs <= INLINE_PORTS
	 || (new_noutputs <= INLINE_PORTS && !new_in_inline));

    // create new port arrays
    Port *new_inputs =
	(new_in_inline ? _inline_ports : new Port[new_ninputs]);
    if (!new_inputs)		// out of memory -- return
	return -ENOMEM;

    Port *new_outputs =
	(new_out_inline ? _inline_ports + (new_in_inline ? new_ninputs : 0)
	 : new Port[new_noutputs]);
    if (!new_outputs) {		// out of memory -- return
	if (!new_in_inline)
	    delete[] new_inputs;
	return -ENOMEM;
    }

    // install information
    if (!old_in_inline)
	delete[] _ports[0];
    if (!old_out_inline)
	delete[] _ports[1];
    _ports[0] = new_inputs;
    _ports[1] = new_outputs;
    _nports[0] = new_ninputs;
    _nports[1] = new_noutputs;
    return 0;
}

/** @brief Returns true iff the element's ports are frozen (deprecated).
 *
 * @deprecated This function is deprecated.  Elements should not set their
 * port counts directly; instead, they should use the port_count() function to
 * give acceptable port count ranges.  In future, ports may be frozen even
 * before configure() runs.
 *
 * An element can change its input and output port counts until after its
 * configure() method returns.  After that point, the ports are frozen, and
 * any attempt to add or remove a port will fail.  The ports_frozen() function
 * returns true once an element's ports are frozen. */
bool
Element::ports_frozen() const
{
    return _router->_state > Router::ROUTER_PRECONFIGURE;
}

/** @brief Called to fetch the element's port count specifier.
 *
 * An element class overrides this virtual function to return a C string
 * describing its port counts.  The string gives acceptable input and output
 * ranges, separated by a slash.  Examples:
 *
 * <dl>
 * <dt><tt>"1/1"</tt></dt> <dd>The element has exactly one input port and one
 * output port.</dd>
 * <dt><tt>"1-2/0"</tt></dt> <dd>One or two input ports and zero output
 * ports.</dd>
 * <dt><tt>"1/-6"</tt></dt> <dd>One input port and up to six output ports.</dd>
 * <dt><tt>"2-/-"</tt></dt> <dd>At least two input ports and any number of
 * output ports.</dd>
 * <dt><tt>"3"</tt></dt> <dd>Exactly three input and output ports.  (If no
 * slash appears, the text is used for both input and output ranges.)</dd>
 * <dt><tt>"1-/="</tt></dt> <dd>At least one input port and @e the @e same
 * number of output ports.</dd>
 * </dl>
 *
 * These ranges help Click determine whether a configuration uses too few or
 * too many ports, and lead to errors such as "'e' has no input 3" and "'e'
 * input 3 unused".
 *
 * Click extracts port count specifiers from the source for use by tools.  For
 * Click to find a port count specifier, the function definition must appear
 * inline, on a single line, inside the element class's declaration, and must
 * return a C string constant.  It should also have public accessibility.
 * Here's an acceptable port_count() definition:
 *
 * @code
 * const char *port_count() const     { return "1/1"; }
 * @endcode
 *
 * The default port_count() method effectively returns @c "0/0".  (In reality,
 * it returns a special value that causes Click to call notify_ninputs() and
 * notify_noutputs(), as in previous releases.  This behavior is deprecated;
 * code should be updated to use port_count().)
 *
 * The following names are available for common port count specifiers.
 *
 * @arg @c PORTS_0_0 for @c "0/0"
 * @arg @c PORTS_0_1 for @c "0/1"
 * @arg @c PORTS_1_0 for @c "1/0"
 * @arg @c PORTS_1_1 for @c "1/1"
 *
 * Since port_count() should return a C string constant with no other
 * processing, it shouldn't matter when it's called; nevertheless, it is
 * called before configure().
 */
const char *
Element::port_count() const
{
    return "";
}

static int
notify_nports_pair(const char *&s, const char *ends, int &lo, int &hi)
{
    if (s == ends || *s == '-')
	lo = 0;
    else if (isdigit(*s))
	s = cp_integer(s, ends, 10, &lo);
    else
	return -1;
    if (s < ends && *s == '-') {
	s++;
	if (s < ends && isdigit(*s))
	    s = cp_integer(s, ends, 10, &hi);
	else
	    hi = INT_MAX;
    } else
	hi = lo;
    return 0;
}

int
Element::notify_nports(int ninputs, int noutputs, ErrorHandler *errh)
{
    const char *s_in = port_count();
    if (!*s_in) {
	notify_ninputs(ninputs);
	notify_noutputs(noutputs);
	return 0;
    }

    const char *s = s_in, *ends = s + strlen(s);
    int ninlo, ninhi, noutlo, nouthi;
    bool equal = false;

    if (notify_nports_pair(s, ends, ninlo, ninhi) < 0)
	goto parse_error;
    
    if (s == ends)
	s = s_in;
    else if (*s == '/')
	s++;
    else
	goto parse_error;

    if (*s == '=' && s + 1 == ends)
	equal = true;
    else if (notify_nports_pair(s, ends, noutlo, nouthi) < 0 || s != ends)
	goto parse_error;

    if (ninputs < ninlo)
	ninputs = ninlo;
    else if (ninputs > ninhi)
	ninputs = ninhi;

    if (equal)
	noutputs = ninputs;
    else if (noutputs < noutlo)
	noutputs = noutlo;
    else if (noutputs > nouthi)
	noutputs = nouthi;

    set_nports(ninputs, noutputs);
    return 0;

  parse_error:
    if (errh)
	errh->error("%{element}: bad port count", this);
    return -1;
}

/** @brief Informs an element how many of its input ports were used
 * (deprecated).
 *
 * @param ninputs number of input ports
 *
 * @deprecated The notify_ninputs() function is deprecated.  Elements should
 * use port_count() instead.
 *
 * Click calls the notify_ninputs() function to inform an element how many of
 * its input ports are used in the configuration.  Thus, if input ports 0-2
 * were used, @a ninputs will be 3.  A typical notify_ninputs() implementation
 * will call set_ninputs() to set the actual number of input ports equal to
 * the number used.
 *
 * notify_ninputs() is called before configure().
 *
 * @sa notify_noutputs, set_ninputs, port_count
 */
void
Element::notify_ninputs(int ninputs)
{
    (void) ninputs;
}

/** @brief Informs an element how many of its output ports were used
 * (deprecated).
 *
 * @param noutputs number of output ports
 *
 * @deprecated The notify_noutputs() function is deprecated.  Elements should
 * use port_count() instead.
 *
 * Click calls the notify_noutputs() function to inform an element how many of
 * its output ports are used in the configuration.  Thus, if output ports 0-2
 * were used, @a noutputs will be 3.  A typical notify_noutputs()
 * implementation will call set_noutputs() to set the actual number of output
 * ports equal to the number used.
 *
 * notify_noutputs() is called after notify_ninputs() and before configure().
 *
 * @sa notify_ninputs, set_noutputs, port_count
 */
void
Element::notify_noutputs(int noutputs)
{
    (void) noutputs;
}

void
Element::initialize_ports(const int *in_v, const int *out_v)
{
    for (int i = 0; i < ninputs(); i++) {
	// allowed iff in_v[i] == VPULL
	int port = (in_v[i] == VPULL ? 0 : -1);
	_ports[0][i] = Port(this, 0, port);
    }
    
    for (int o = 0; o < noutputs(); o++) {
	// allowed iff out_v[o] != VPULL
	int port = (out_v[o] == VPULL ? -1 : 0);
	_ports[1][o] = Port(this, 0, port);
    }
}

int
Element::connect_port(bool isoutput, int port, Element* e, int e_port)
{
    if (port_active(isoutput, port)) {
	_ports[isoutput][port] = Port(this, e, e_port);
	return 0;
    } else
	return -1;
}


// FLOW

/** @brief Called to fetch the element's internal packet flow specifier (its
 * <em>flow code</em>).
 *
 * An element class overrides this virtual function to return a C string
 * describing how packets flow within the element.  That is, can packets that
 * arrive on input port X be emitted on output port Y, for all X and Y?  This
 * information helps Click answer questions such as "What Queues are
 * downstream of this element?" and "Should this agnostic port be push or
 * pull?".  See below for more.
 *
 * A flow code string consists of an input specification and an output
 * specification, separated by a slash.  Each specification is a sequence of
 * @e port @e codes.  Packets can travel from an input port to an output port
 * only if the port codes match.
 *
 * The simplest port code is a single case-sensitive letter.  For example, the
 * flow code @c "x/x" says that packets can travel from the element's input
 * port to its output port, while @c "x/y" says that packets never travel
 * between ports.
 *
 * A port code may also be a sequence of letters in brackets, such as
 * <tt>[abz]</tt>. Two port codes match iff they have at least one letter in
 * common, so <tt>[abz]</tt> matches <tt>a</tt>, but <tt>[abz]</tt> and
 * <tt>[cde]</tt> do not match. The opening bracket may be followed by a caret
 * <tt>^</tt>; this makes the port code match letters @e not mentioned between
 * the brackets. Thus, the port code <tt>[^bc]</tt> is equivalent to
 * <tt>[ABC...XYZadef...xyz]</tt>.
 *
 * Finally, the @c # character is also a valid port code, and may be used
 * within brackets.  One @c # matches another @c # only when they represent
 * the same port number -- for example, when one @c # corresponds to input
 * port 2 and the other to output port 2.  @c # never matches any letter.
 * Thus, for an element with exactly 2 inputs and 2 outputs, the flow code @c
 * "##/##" behaves like @c "xy/xy".
 *
 * The last code in each specification is duplicated as many times as
 * necessary, and any extra codes are ignored.  The flow codes @c
 * "[x#][x#][x#]/x######" and @c "[x#]/x#" behave identically.
 *
 * Here are some example flow codes.
 *
 * <dl>
 * <dt><tt>"x/x"</tt></dt>
 * <dd>Packets may travel from any input port to any output port.  Most
 * elements use this flow code.</dd>
 *
 * <dt><tt>"xy/x"</tt></dt>
 * <dd>Packets arriving on input port 0 may travel to any output port, but
 * those arriving on other input ports will not be emitted on any output.
 * @e ARPQuerier uses this flow code.</dd>
 *
 * <dt><tt>"x/y"</tt></dt> <dd>Packets never travel between input and output
 * ports. @e Idle and @e Error use this flow code.  So does @e KernelTun,
 * since its input port and output port are decoupled (packets received on its
 * input are sent to the kernel; packets received from the kernel are sent to
 * its output).</dd>
 *
 * <dt><tt>"#/#"</tt></dt> <dd>Packets arriving on input port @e K may travel
 * only to output port @e K.  @e Suppressor uses this flow code.</dd>
 *
 * <dt><tt>"#/[^#]"</tt></dt> <dd>Packets arriving on input port @e K may
 * travel to any output port except @e K.  @e EtherSwitch uses this flow
 * code.</dd> </dl>
 *
 * Click extracts flow codes from the source for use by tools.  For Click to
 * find a flow code, the function definition must appear inline, on a single
 * line, inside the element class's declaration, and must return a C string
 * constant.  It should also have public accessibility.  Here's an acceptable
 * flow_code() definition:
 *
 * @code
 * const char *flow_code() const     { return "xy/x"; }
 * @endcode
 *
 * The default flow_code() method returns @c "x/x", which indicates that
 * packets may travel from any input to any output.  This default is
 * acceptable for the vast majority of elements.
 *
 * The following name is available for a common flow code.
 *
 * @arg @c COMPLETE_FLOW for @c "x/x"
 *
 * Since flow_code() should return a C string constant with no other
 * processing, it shouldn't matter when it's called; nevertheless, it is
 * called before configure().
 *
 * <h3>Determining an element's flow code</h3>
 *
 * What does it mean for a packet to travel from one port to another?  To pick
 * the right flow code for an element, consider how a flow code would affect a
 * simple router.
 *
 * Given an element @e E with input port @e M and output port @e N, imagine
 * this simple configuration (or a similar configuration):
 * 
 * <tt>... -> RED -> [@e M] E [@e N] -> Queue -> ...;</tt>
 *
 * Now, should the @e RED element include the @e Queue element in its queue
 * length calculation?  If so, then the flow code's <em>M</em>th input port
 * code and <em>N</em>th output port code should match.  If not, they
 * shouldn't.
 *
 * For example, consider @e ARPQuerier's second input port.  On receiving an
 * ARP response on that input, @e ARPQuerier may emit a held-over IP packet to
 * its first output.  However, a @e RED element upstream of that second input
 * port probably wouldn't count the downstream @e Queue in its queue length
 * calculation.  After all, the ARP responses are effectively dropped; packets
 * emitted onto the @e Queue originally came from @e ARPQuerier's first input
 * port.  Therefore, @e ARPQuerier's flow code, <tt>"xy/x"</tt>, specifies
 * that packets arriving on the second input port are not emitted on any
 * output port.
 *
 * The @e ARPResponder element provides a contrasting example.  It has one
 * input port, which receives ARP queries, and one output port, which emits
 * the corresponding ARP responses.  A @e RED element upstream of @e
 * ARPResponder probably @e would want to include a downstream @e Queue, since
 * queries received by @e ARPResponder are effectively transmuted into emitted
 * responses. Thus, @e ARPResponder's flow code, <tt>"x/x"</tt> (the default),
 * specifies that packets travel through it, even though the packets it emits
 * are completely different from the packets it receives.
 *
 * If you find this confusing, don't fret.  It is perfectly fine to be
 * conservative when assigning flow codes, and the vast majority of the Click
 * distribution's elements use @c COMPLETE_FLOW.
 */
const char *
Element::flow_code() const
{
    return COMPLETE_FLOW;
}

static void
skip_flow_code(const char*& p)
{
    if (*p != '/' && *p != 0) {
	if (*p == '[') {
	    for (p++; *p != ']' && *p; p++)
		/* nada */;
	    if (*p)
		p++;
	} else
	    p++;
    }
}

static int
next_flow_code(const char*& p, int port, Bitvector& code, ErrorHandler* errh, const Element* e)
{
    if (*p == '/' || *p == 0) {
	// back up to last code character
	if (p[-1] == ']') {
	    for (p -= 2; *p != '['; p--)
		/* nada */;
	} else
	    p--;
    }

    code.assign(256, false);

    if (*p == '[') {
	bool negated = false;
	if (p[1] == '^')
	    negated = true, p++;
	for (p++; *p != ']' && *p; p++) {
	    // no isalpha: avoid locale and signed char dependencies
	    if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z'))
		code[*p] = true;
	    else if (*p == '#')
		code[port + 128] = true;
	    else if (errh)
		errh->error("'%{element}' flow code: invalid character '%c'", e, *p);
	}
	if (negated)
	    code.negate();
	if (!*p) {
	    if (errh)
		errh->error("'%{element}' flow code: missing ']'", e);
	    p--;			// don't skip over final '\0'
	}
    } else if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z'))
	code[*p] = true;
    else if (*p == '#')
	code[port + 128] = true;
    else {
	if (errh)
	    errh->error("'%{element}' flow code: invalid character '%c'", e, *p);
	p++;
	return -1;
    }

    p++;
    return 0;
}

/** @brief Analyze internal packet flow with respect to @a port.
 *
 * @param isoutput false for input ports, true for output ports
 * @param port port number
 * @param[out] travels the bitvector to initialize with internal packet flow
 * information
 *
 * Analyzes the element's flow_code() and determines how packets might travel
 * from the specified port.  The @a travels bitvector is initialized to have
 * one entry per complementary port; thus, if @a isoutput is true, then @a
 * travels has ninputs() entries.  The entry for port @a p is set to true iff
 * packets can travel from @a port to @a p.  Returns all false if @a port is
 * out of range.
 *
 * For example, if flow_code() is "xy/xxyx", and the element has 2 inputs and
 * 4 outputs, then:
 *
 *  - port_flow(false, 0, travels) returns [true, true, false, true]
 *  - port_flow(false, 1, travels) returns [false, false, true, false]
 *  - port_flow(true, 0, travels) returns [true, false]
 *
 * @sa flow_code
 */
void
Element::port_flow(bool isoutput, int port, Bitvector* travels) const
{
    const char *f = flow_code();
    int nother = nports(!isoutput);
    if (port < 0 || port >= nports(isoutput)) {
	travels->assign(nother, false);
	return;
    } else if (!f || f == COMPLETE_FLOW) {
	travels->assign(nother, true);
	return;
    }

    travels->assign(nother, false);
    ErrorHandler* errh = ErrorHandler::default_handler();
  
    const char* f_in = f;
    const char* f_out = strchr(f, '/');
    f_out = (f_out ? f_out + 1 : f_in);
    if (*f_out == '\0' || *f_out == '/') {
	errh->error("'%{element}' flow code: missing or bad '/'", this);
	return;
    }

    if (isoutput) {
	const char* f_swap = f_in;
	f_in = f_out;
	f_out = f_swap;
    }
  
    Bitvector in_code;
    for (int i = 0; i < port; i++)
	skip_flow_code(f_in);
    next_flow_code(f_in, port, in_code, errh, this);

    Bitvector out_code;
    for (int i = 0; i < nother; i++) {
	next_flow_code(f_out, i, out_code, errh, this);
	if (in_code.nonzero_intersection(out_code))
	    (*travels)[i] = true;
    }
}


// PUSH OR PULL PROCESSING

/** @brief Called to fetch the element's processing specifier.
 *
 * An element class overrides this virtual function to return a C string
 * describing which of its ports are push, pull, or agnostic.  The string
 * gives acceptable input and output ranges, separated by a slash; the
 * characters @c "h", @c "l", and @c "a" indicate push, pull, and agnostic
 * ports, respectively.  Examples:
 *
 * @arg @c "h/h" All input and output ports are push.
 * @arg @c "h/l" Push input ports and pull output ports.
 * @arg @c "a/ah" All input ports are agnostic.  The first output port is also
 * agnostic, but the second and subsequent output ports are push.
 * @arg @c "hl/hlh" Input port 0 and output port 0 are push.  Input port 1 and
 * output port 1 are pull.  All remaining inputs are pull; all remaining
 * outputs are push.
 * @arg @c "a" All input and output ports are agnostic.  (If no slash appears,
 * the text is used for both input and output ports.)
 *
 * Thus, each character indicates a single port's processing type, except that
 * the last character in the input section is used for all remaining input
 * ports (and similarly for outputs).  It's OK to have more characters than
 * ports; any extra characters are ignored.
 *
 * Click extracts processing specifiers from the source for use by tools.  For
 * Click to find a processing specifier, the function definition must appear
 * inline, on a single line, inside the element class's declaration, and must
 * return a C string constant.  It should also have public accessibility.
 * Here's an acceptable processing() definition:
 *
 * @code
 * const char *processing() const     { return "a/ah"; }
 * @endcode
 *
 * The default processing() method returns @c "a/a", which sets all ports to
 * agnostic.
 *
 * The following names are available for common processing specifiers.
 *
 * @arg @c AGNOSTIC for @c "a/a"
 * @arg @c PUSH for @c "h/h"
 * @arg @c PULL for @c "l/l"
 * @arg @c PUSH_TO_PULL for @c "h/l"
 * @arg @c PULL_TO_PUSH for @c "l/h"
 *
 * Since processing() should return a C string constant with no other
 * processing, it shouldn't matter when it's called; nevertheless, it is
 * called before configure().
 */
const char*
Element::processing() const
{
    return AGNOSTIC;
}

int
Element::next_processing_code(const char*& p, ErrorHandler* errh)
{
    switch (*p) {
    
      case 'h': case 'H':
	p++;
	return Element::VPUSH;
    
      case 'l': case 'L':
	p++;
	return Element::VPULL;
    
      case 'a': case 'A':
	p++;
	return Element::VAGNOSTIC;

      case '/': case 0:
	return -2;

      default:
	if (errh)
	    errh->error("bad processing code");
	p++;
	return -1;
    
    }
}

void
Element::processing_vector(int* in_v, int* out_v, ErrorHandler* errh) const
{
    const char* p_in = processing();
    int val = 0;

    const char* p = p_in;
    int last_val = 0;
    for (int i = 0; i < ninputs(); i++) {
	if (last_val >= 0)
	    last_val = next_processing_code(p, errh);
	if (last_val >= 0)
	    val = last_val;
	in_v[i] = val;
    }

    while (*p && *p != '/')
	p++;
    if (!*p)
	p = p_in;
    else
	p++;

    last_val = 0;
    for (int i = 0; i < noutputs(); i++) {
	if (last_val >= 0)
	    last_val = next_processing_code(p, errh);
	if (last_val >= 0)
	    val = last_val;
	out_v[i] = val;
    }
}

const char*
Element::flags() const
{
    return "";
}

// CLONING AND CONFIGURING

/** @brief Called to fetch the element's configure phase, which determines the
 * order in which elements are configured and initialized.
 *
 * Click configures and initializes elements in increasing order of
 * configure_phase().  An element with configure phase 1 will always be
 * configured (have its configure() method called) before an element with
 * configure phase 2.  Thus, if two element classes must be configured in a
 * given order, they should define configure_phase() functions to enforce that
 * order.  For example, the @e AddressInfo element defines address
 * abbreviations for other elements to use; it should thus be configured
 * before other elements, and its configure_phase() method returns a low
 * value.
 *
 * Configure phases should be defined relative to the following constants,
 * which are listed in increasing order.
 *
 * <dl>
 * <dt><tt>CONFIGURE_PHASE_FIRST</tt></dt>
 * <dd>Configure before other elements.  Used by @e AddressInfo.</dd>
 *
 * <dt><tt>CONFIGURE_PHASE_INFO</tt></dt>
 * <dd>Configure early.  Appropriate for most information elements, such as @e ScheduleInfo.</dd>
 *
 * <dt><tt>CONFIGURE_PHASE_PRIVILEGED</tt></dt>
 * <dd>Intended for elements that require root
 * privilege when run at user level, such as @e FromDevice and
 * @e ToDevice.  The @e ChangeUID element, which reliquishes root
 * privilege, runs at configure phase @c CONFIGURE_PHASE_PRIVILEGED + 1.</dd>
 *
 * <dt><tt>CONFIGURE_PHASE_DEFAULT</tt></dt> <dd>The default implementation
 * returns @c CONFIGURE_PHASE_DEFAULT, so most elements are configured at this
 * phase.  Appropriate for most elements.</dd>
 *
 * <dt><tt>CONFIGURE_PHASE_LAST</tt></dt>
 * <dd>Configure after other elements.</dd>
 * </dl>
 *
 * The body of a configure_phase() method should consist of a single @c return
 * statement returning some constant.  Although it shouldn't matter when it's
 * called, it is called before configure().
 */
int
Element::configure_phase() const
{
    return CONFIGURE_PHASE_DEFAULT;
}

/** @brief Called to parse the element's configuration arguments.
 *
 * @param conf configuration arguments
 * @param errh error handler
 *
 * The configure() method is passed the element's configuration arguments.  It
 * should parse them, report any errors, and initialize the element's internal
 * state.
 *
 * The @a conf argument is the element's configuration string, divided into
 * configuration arguments by splitting at commas and removing comments and
 * leading and trailing whitespace (see cp_argvec()).  If @a conf is empty,
 * the element was not supplied with a configuration string (or its
 * configuration string contained only comments and whitespace).  It is safe
 * to modify @a conf; modifications will be thrown away when the function
 * returns.
 *
 * Any errors, warnings, or messages should be reported to @a errh.  Messages
 * need not specify the element name or type, since this information will be
 * provided as context.
 *
 * configure() should return a negative number if configuration fails.
 * Returning a negative number prevents the router from initializing.  The
 * default configure() method succeeds if and only if there are no
 * configuration arguments.
 *
 * configure() methods are called in order of configure_phase().  All
 * elements' configure() methods are called, even if an early configure()
 * method fails; this is to report all relevant error messages to the user,
 * rather than just the first.
 *
 * configure() is called early in the initialization process, and cannot check
 * whether a named handler exists.  That function must be left for
 * initialize().  Assuming all router connections are valid and all
 * configure() methods succeed, the add_handlers() functions will be called
 * next.
 *
 * A configure() method should avoid potentially harmful actions, such
 * as truncating files or attaching to devices.  These actions should be left
 * for the initialize() method, which is called later.  This avoids harm if
 * another element cannot be configured, or if the router is incorrectly
 * connected, since in those cases initialize() will never be called.
 *
 * Elements that support live reconfiguration (see can_live_reconfigure())
 * should expect configure() to be called at run time, when a user writes to
 * the element's @c config handler.  In that case, configure() must be careful
 * not to disturb the existing configuration unless the new configuration is
 * error-free.
 *
 * @note In previous releases, configure() could not determine whether a port
 * is push or pull or query the router for information about neighboring
 * elements.  Those functions had to be left for initialize().  Even in the
 * current release, if any element in a configuration calls the deprecated
 * set_ninputs() or set_noutputs() function from configure(), then all push,
 * pull, and neighbor information is invalidated until initialize() time.
 *
 * @sa live_reconfigure
 */
int
Element::configure(Vector<String> &conf, ErrorHandler *errh)
{
    return cp_va_parse(conf, this, errh, cpEnd);
}

/** @brief Called when an element should install its handlers.
 *
 * The add_handlers() method should install any handlers the element provides
 * by calling add_read_handler(), add_write_handler(), and set_handler().
 * These functions may also be called from configure(), initialize(), or even
 * later, during router execution.  However, it is better in most cases to
 * initialize handlers in configure() or add_handlers(), since elements that
 * depend on other handlers often check in initialize() whether those handlers
 * exist.
 *
 * add_handlers() is called after configure() and before initialize().  When
 * it runs, it is guaranteed that every configure() method succeeded and that
 * all connections are correct (push and pull match up correctly and there are
 * no unused or badly-connected ports).
 * 
 * Most add_handlers() methods simply call add_read_handler(),
 * add_write_handler(), add_task_handlers(), and possibly set_handler() one or
 * more times.  The default add_handlers() method does nothing.
 *
 * Click automatically provides five handlers for each element: @c class, @c
 * name, @c config, @c ports, and @c handlers.  There is no need to provide
 * these yourself.
 */
void
Element::add_handlers()
{
}

/** @brief Called to initialize an element.
 *
 * @param errh error handler
 *
 * The initialize() method is called just before the router is placed on
 * line. It performs any final initialization, and provides the last chance to
 * abort router installation with an error.  Any errors, warnings, or messages
 * should be reported to @a errh.  Messages need not specify the element
 * name; this information will be supplied externally.
 *
 * initialize() should return zero if initialization succeeds, or a negative
 * number if it fails.  Returning a negative number prevents the router from
 * initializing.  The default initialize() method always returns zero
 * (success).
 *
 * initialize() methods are called in order of configure_phase(), using the
 * same order as for configure().  When an initialize() method fails, router
 * initialization stops immediately, and no more initialize() methods are
 * called.  Thus, at most one initialize() method can fail per router
 * configuration.
 *
 * initialize() is called after add_handlers() and before take_state().  When
 * it runs, it is guaranteed that every configure() method succeeded, that all
 * connections are correct (push and pull match up correctly and there are no
 * unused or badly-connected ports), and that every add_handlers() method has
 * been called.
 *
 * If every element's initialize() method succeeds, then the router is
 * installed, and will remain installed until another router replaces it.  Any
 * errors that occur later than initialize() -- during take_state(), push(),
 * or pull(), for example -- will not take the router off line.
 *
 * Strictly speaking, the only task that @e must go in initialize() is
 * checking whether a handler exists, since that information isn't available
 * at configure() time.  It's often convenient, however, to put other
 * functionality in initialize().  For example, opening files for writing fits
 * well in initialize(): if the configuration has errors before the relevant
 * element is initialized, any existing file will be left as is.  Common tasks
 * performed in initialize() methods include:
 *
 *   - Initializing Task objects.
 *   - Allocating memory.
 *   - Opening files.
 *   - Initializing network devices.
 *
 * @note initialize() methods may not create or destroy input and output
 * ports, but this functionality is deprecated anyway.
 *
 * @note In previous releases, configure() could not determine whether a port
 * was push or pull or query the router for information about neighboring
 * elements, so those tasks were relegated to initialize() methods.  In the
 * current release, configure() can perform these tasks too.
 */
int
Element::initialize(ErrorHandler *errh)
{
    (void) errh;
    return 0;
}

/** @brief Called when this element should take @a old_element's state, if
 * possible, during a hotswap operation.
 *
 * @param old_element element in the old configuration; result of
 * hotswap_element()
 * @param errh error handler
 *
 * The take_state() method supports hotswapping, and is the last stage of
 * configuration installation.  When a configuration is successfully installed
 * with the hotswap option, the driver (1) stops the old configuration, (2)
 * searches the two configurations for pairs of compatible elements, (3) calls
 * take_state() on the new elements in those pairs to give them a chance to
 * take state from the old elements, and (4) starts the new configuration.
 *
 * take_state() is called only when a configuration is hotswapped in.  The
 * default take_state() implementation does nothing; there's no need to
 * override it unless your element has state you want preserved across
 * hotswaps.
 *
 * The @a old_element argument is an element from the old configuration (that
 * is, from router()->@link Router::hotswap_router() hotswap_router()@endlink)
 * obtained by calling hotswap_element().  If hotswap_element() returns null,
 * take_state() will not be called.  The default hotswap_element() returns an
 * @a old_element has the same id() as this element.  This is often too loose;
 * for instance, @a old_element might have a completely different class.
 * Thus, most take_state() methods begin by attempting to cast() @a
 * old_element to a compatible class, and silently returning if the result is
 * null.  Alternatively, you can override hotswap_element() and put the check
 * there.
 *
 * Errors and warnings should be reported to @a errh, but the router will be
 * installed whether or not there are errors.  take_state() should always
 * leave this element in a state that's safe to run, and @a old_element in a
 * state that's safe to cleanup().
 *
 * take_state() is called after initialize().  When it runs, it is guaranteed
 * that this element's configuration will shortly be installed.  Every
 * configure() and initialize() method succeeded, all connections are correct
 * (push and pull match up correctly and there are no unused or
 * badly-connected ports), and every add_handlers() method has been called.
 * It is also guaranteed that the old configuration (of which old_element is a
 * part) had been successfully installed, but that none of its tasks are
 * running at the moment.
 */
void
Element::take_state(Element *old_element, ErrorHandler *errh)
{
    (void) old_element, (void) errh;
}

/** @brief Returns a compatible element in the hotswap router.
 *
 * hotswap_element() searches the hotswap router, router()->@link
 * Router::hotswap_router() hotswap_router()@endlink, for an element
 * compatible with this element.  It returns that element, if any.  If there's
 * no compatible element, or no hotswap router, then it returns 0.
 *
 * The default implementation searches for an element with the same name as
 * this element.  Thus, it returns 0 or an element that satisfies this
 * constraint: hotswap_element()->id() == id().
 *
 * Generally, this constraint is too loose.  A @e Queue element can't hotswap
 * state from an @e ARPResponder, even if they do have the same name.  Most
 * elements also check that hotswap_element() has the right class, using the
 * cast() function.  This check can go either in hotswap_element() or in
 * take_state() itself, whichever is easier; Click doesn't use the result of
 * hotswap_element() except as an argument to take_state().
 *
 * @sa take_state, Router::hotswap_router
 */
Element *
Element::hotswap_element() const
{
    if (Router *r = router()->hotswap_router())
	if (Element *e = r->find(id()))
	    return e;
    return 0;
}

/** @brief Called when an element should clean up its state.
 *
 * @param stage this element's maximum initialization stage
 *
 * The cleanup() method should clean up any state allocated by the
 * initialization process.  For example, it should close any open files, free
 * up memory, and unhook from network devices.  Click calls cleanup() when it
 * determines that an element's state is no longer needed, either because a
 * router configuration is about to be removed or because the router
 * configuration failed to initialize properly.  Click will call the cleanup()
 * method exactly once on every element it creates.
 *
 * The @a stage parameter is an enumeration constant indicating how far the
 * element made it through the initialization process.  Possible values are,
 * in increasing order:
 *
 * <dl>
 * <dt><tt>CLEANUP_NO_ROUTER</tt></dt>
 * <dd>The element was never attached to a router.</dd>
 *
 * <dt><tt>CLEANUP_CONFIGURE_FAILED</tt></dt>
 * <dd>The element's configure() method was called, but it failed.</dd>
 *
 * <dt><tt>CLEANUP_CONFIGURED</tt></dt> <dd>The element's configure() method
 * was called and succeeded, but its initialize() method was not called
 * (because some other element's configure() method failed, or there was a
 * problem with the configuration's connections).</dd>
 *
 * <dt><tt>CLEANUP_INITIALIZE_FAILED</tt></dt> <dd>The element's configure()
 * and initialize() methods were both called.  configure() succeeded, but
 * initialize() failed.</dd>
 *
 * <dt><tt>CLEANUP_INITIALIZED</tt></dt> <dd>The element's configure() and
 * initialize() methods were called and succeeded, but its router was never
 * installed (because some other element's initialize() method failed).</dd>
 *
 * <dt><tt>CLEANUP_ROUTER_INITIALIZED</tt></dt> <dd>The element's configure()
 * and initialize() methods were called and succeeded, and the router of which
 * it is a part was successfully installed.</dd>
 *
 * <dt><tt>CLEANUP_MANUAL</tt></dt> <dd>Never used by Click.  Intended for use
 * when element code calls cleanup() explicitly.</dd>
 * </dl>
 *
 * A configuration's cleanup() methods are called in the reverse of the
 * configure_phase() order used for configure() and initialize().
 *
 * The default cleanup() method does nothing.
 *
 * cleanup() serves some of the same functions as an element's destructor.
 * However, cleanup() may be called long before an element is destroyed.
 * Elements that are part of an erroneous router are cleaned up, but kept
 * around for debugging purposes until another router is installed.
 */
void
Element::cleanup(CleanupStage stage)
{
    (void) stage;
}


// LIVE CONFIGURATION

/** @brief Called to check whether an element supports live reconfiguration.
 *
 * Returns true iff this element can be reconfigured as the router is running.
 * Click will make the element's "config" handler writable if
 * can_live_reconfigure() returns true; when that handler is written, Click
 * will call the element's live_reconfigure() function.  The default
 * implementation returns false.
 */
bool
Element::can_live_reconfigure() const
{
  return false;
}

/** @brief Called to reconfigure an element while the router is running.
 *
 * @param conf configuration arguments
 * @param errh error handler
 *
 * This function should parse the configuration arguments in @a conf, set the
 * element's state accordingly, and report any error messages or warnings to
 * @a errh.  This resembles configure().  However, live_reconfigure() is
 * called when the element's "config" handler is written, rather than at
 * router initialization time.  Thus, the element already has a working
 * configuration.  If @a conf has an error, live_reconfigure() should leave
 * this previous working configuration alone.
 *
 * can_live_reconfigure() must return true for live_reconfigure() to work.
 *
 * Return >= 0 on success, < 0 on error.  On success, Click will set the
 * element's old configuration arguments to @a conf, so that later reads of
 * the "config" handler will return @a conf.  (A non-default configuration()
 * method can override this.)
 *
 * The default implementation simply calls configure(@a conf, @a errh).  This
 * is OK as long as configure() doesn't change the element's state on error.
 *
 * @sa can_live_reconfigure
 */
int
Element::live_reconfigure(Vector<String> &conf, ErrorHandler *errh)
{
  if (can_live_reconfigure())
    return configure(conf, errh);
  else
    return errh->error("cannot reconfigure %{element} live", this);
}


// used by configuration() and reconfigure_handler()
static int store_default_configuration;
static int was_default_configuration;

/** @brief Called to fetch the element's current configuration arguments.
 *
 * @param conf vector into which the arguments are appended.  Must be empty.
 *
 * The default implementation breaks the element's stored configuration string
 * into arguments using cp_argvec().  Some elements override
 * configuration(Vector<String> &) to return a configuration as updated by
 * later events, such as handlers, by extracting the configuration from
 * current element state.
 */
void
Element::configuration(Vector<String> &conf) const
{
  // Handle configuration(void) requests specially by preserving whitespace.
  String s = router()->default_configuration_string(eindex());
  if (store_default_configuration)
    conf.push_back(s);
  else
    cp_argvec(s, conf);
  was_default_configuration = 1;
}

/** @brief Returns the element's current configuration string.
 *
 * The configuration string is obtained by fetching the current configuration
 * arguments (using configuration(Vector<String> &)) and joining them with
 * ", " (using cp_argvec()).  Cannot be overridden.
 */
String
Element::configuration() const
{
  store_default_configuration = 1;
  Vector<String> conf;
  configuration(conf);
  store_default_configuration = 0;
  // cp_unargvec(conf) will return conf[0] if conf has one element, so
  // store_default_configuration will work as expected.
  return cp_unargvec(conf);
}


// SELECT

#if CLICK_USERLEVEL

/** @brief Called to handle a file descriptor event.
 *
 * @param fd the file descriptor
 *
 * Click's call to select() indicates that the file descriptor @a fd is
 * readable, writable, or both.  The overriding method should read or write
 * the file descriptor as appropriate.  The default implementation causes an
 * assertion failure.
 *
 * The element must have previously registered interest in @a fd with
 * add_select().
 *
 * @note Only available at user level.
 *
 * @sa add_select, remove_select
 */
void
Element::selected(int fd)
{
    assert(0 /* selected not overridden */);
    (void) fd;
}

/** @brief Register interest in @a mask events on file descriptor @a fd.
 *
 * @param fd the file descriptor
 * @param mask relevant events: bitwise-or of one or more of SELECT_READ, SELECT_WRITE
 *
 * Click will register interest in readability and/or writability on file
 * descriptor @a fd.  When @a fd is ready, Click will call this element's
 * selected(@a fd) method.
 *
 * add_select(@a fd, @a mask) overrides any previous add_select() for the same
 * @a fd and events in @a mask.  However, different elements may register
 * interest in different events for the same @a fd.
 *
 * @note Only available at user level.
 *
 * @note Selecting for writability with SELECT_WRITE normally requires more
 * care than selecting for readability with SELECT_READ.  You should
 * add_select(@a fd, SELECT_WRITE) only when there is data to write to @a fd.
 * Otherwise, Click will constantly poll your element's selected(@a fd)
 * method.
 *
 * @sa remove_select, selected
 */
int
Element::add_select(int fd, int mask)
{
    return master()->add_select(fd, this, mask);
}

/** @brief Remove interest in @a mask events on file descriptor @a fd.
 *
 * @param fd the file descriptor
 * @param mask relevant events: bitwise-or of one or more of SELECT_READ, SELECT_WRITE
 *
 * Click will remove any existing add_select() registrations for readability
 * and/or writability on file descriptor @a fd.  The named events on @a fd
 * will no longer cause a selected() call.
 *
 * @note Only available at user level.
 *
 * @sa add_select, selected
 */
int
Element::remove_select(int fd, int mask)
{
    return master()->remove_select(fd, this, mask);
}

#endif


// HANDLERS

/** @brief Register a read handler named @a name.
 *
 * @param name handler name
 * @param hook function called when handler is read
 * @param thunk user data parameter passed to @a hook
 *
 * Adds a read handler named @a name for this element.  Reading the handler
 * returns the result of the @a hook function, which is called like this:
 *
 * @code
 * String result = hook(e, thunk);
 * @endcode
 *
 * @a e is this element pointer.
 *
 * add_read_handler(@a name) overrides any previous
 * add_read_handler(@a name) or set_handler(@a name), but any previous
 * add_write_handler(@a name) remains in effect.
 *
 * @sa read_positional_handler, read_keyword_handler: standard read handler
 * hook functions
 * @sa add_write_handler, set_handler, add_task_handlers
 */
void
Element::add_read_handler(const String &name, ReadHandlerHook hook, void *thunk)
{
    Router::add_read_handler(this, name, hook, thunk);
}

/** @brief Register a write handler named @a name.
 *
 * @param name handler name
 * @param hook function called when handler is written
 * @param thunk user data parameter passed to @a hook
 *
 * Adds a write handler named @a name for this element.  Writing the handler
 * calls the @a hook function like this:
 *
 * @code
 * int r = hook(data, e, thunk, errh);
 * @endcode
 *
 * @a e is this element pointer.  The return value @a r should be negative on
 * error, positive or zero on success.  Any messages should be reported to the
 * @a errh ErrorHandler object.
 *
 * add_write_handler(@a name) overrides any previous
 * add_write_handler(@a name) or set_handler(@a name), but any previous
 * add_read_handler(@a name) remains in effect.
 *
 * @sa reconfigure_positional_handler, reconfigure_keyword_handler: standard
 * write handler hook functions
 * @sa add_read_handler, set_handler, add_task_handlers
 */
void
Element::add_write_handler(const String &name, WriteHandlerHook hook, void *thunk)
{
    Router::add_write_handler(this, name, hook, thunk);
}

/** @brief Register a comprehensive handler named @a name.
 *
 * @param name handler name
 * @param flags handler flags
 * @param hook function called when handler is written
 * @param thunk1 user data parameter stored in the handler
 * @param thunk2 user data parameter stored in the handler
 *
 * Registers a comprehensive handler named @a name for this element.  The
 * handler handles the operations specified by @a flags, which can include
 * Handler::OP_READ, Handler::OP_WRITE, Handler::READ_PARAM, and others.
 * Reading the handler calls the @a hook function like this:
 *
 * @code
 * String data;
 * int r = hook(Handler::OP_READ, data, e, h, errh);
 * @endcode
 *
 * Writing the handler calls it like this:
 *
 * @code
 * int r = hook(Handler::OP_WRITE, data, e, h, errh);
 * @endcode
 *
 * @a e is this element pointer, and @a h points to the Handler object for
 * this handler.  The @a data string is an out parameter for reading and an in
 * parameter for writing; when reading with parameters, @a data has the
 * parameters on input and should be replaced with the result on output.  The
 * return value @a r should be negative on error, positive or zero on success.
 * Any messages should be reported to the @a errh ErrorHandler object.
 *
 * set_handler(@a name) overrides any previous
 * add_read_handler(@a name), add_write_handler(@a name), or set_handler(@a
 * name).
 */
void
Element::set_handler(const String& name, int flags, HandlerHook hook, void* thunk1, void* thunk2)
{
    Router::set_handler(this, name, flags, hook, thunk1, thunk2);
}

static String
read_class_handler(Element *e, void *)
{
    return String(e->class_name()) + "\n";
}

static String
read_name_handler(Element *e, void *)
{
  return e->id() + "\n";
}

static String
read_config_handler(Element *e, void *)
{
    String s = e->configuration();
    if (s.length() && s.back() != '\n')
	return s + "\n";
    else
	return s;
}

static int
write_config_handler(const String &str, Element *e, void *,
		     ErrorHandler *errh)
{
    Vector<String> conf;
    cp_argvec(str, conf);
    int r = e->live_reconfigure(conf, errh);
    if (r >= 0)
	e->router()->set_default_configuration_string(e->eindex(), str);
    return r;
}

static String
read_ports_handler(Element *e, void *)
{
    return e->router()->element_ports_string(e->eindex());
}

static String
read_handlers_handler(Element *e, void *)
{
    Vector<int> hindexes;
    Router::element_hindexes(e, hindexes);
    StringAccum sa;
    for (int* hip = hindexes.begin(); hip < hindexes.end(); hip++) {
	const Handler* h = Router::handler(e, *hip);
	if (h->read_visible() || h->write_visible())
	    sa << h->name() << '\t' << (h->read_visible() ? "r" : "") << (h->write_visible() ? "w" : "") << '\n';
    }
    return sa.take_string();
}


#if CLICK_STATS >= 1

static String
read_icounts_handler(Element *f, void *)
{
  StringAccum sa;
  for (int i = 0; i < f->ninputs(); i++)
    if (f->input(i).allowed() || CLICK_STATS >= 2)
      sa << f->input(i).npackets() << "\n";
    else
      sa << "??\n";
  return sa.take_string();
}

static String
read_ocounts_handler(Element *f, void *)
{
  StringAccum sa;
  for (int i = 0; i < f->noutputs(); i++)
    if (f->output(i).allowed() || CLICK_STATS >= 2)
      sa << f->output(i).npackets() << "\n";
    else
      sa << "??\n";
  return sa.take_string();
}

#endif /* CLICK_STATS >= 1 */

#if CLICK_STATS >= 2
/*
 * cycles:
 * # of calls to this element (push or pull).
 * cycles spent in this element and elements it pulls or pushes.
 * cycles spent in the elements this one pulls and pushes.
 */
static String
read_cycles_handler(Element *f, void *)
{
  return(String(f->_calls) + "\n" +
         String(f->_self_cycles) + "\n" +
         String(f->_child_cycles) + "\n");
}
#endif

void
Element::add_default_handlers(bool allow_write_config)
{
  add_read_handler("class", read_class_handler, 0);
  add_read_handler("name", read_name_handler, 0);
  add_read_handler("config", read_config_handler, 0);
  if (allow_write_config && can_live_reconfigure())
    add_write_handler("config", write_config_handler, 0);
  add_read_handler("ports", read_ports_handler, 0);
  add_read_handler("handlers", read_handlers_handler, 0);
#if CLICK_STATS >= 1
  add_read_handler("icounts", read_icounts_handler, 0);
  add_read_handler("ocounts", read_ocounts_handler, 0);
# if CLICK_STATS >= 2
  add_read_handler("cycles", read_cycles_handler, 0);
# endif
#endif
}

#ifdef HAVE_STRIDE_SCHED
static String
read_task_tickets(Element *e, void *thunk)
{
  Task *task = (Task *)((uint8_t *)e + (intptr_t)thunk);
  return String(task->tickets()) + "\n";
}

static int
write_task_tickets(const String &s, Element *e, void *thunk, ErrorHandler *errh)
{
  Task *task = (Task *)((uint8_t *)e + (intptr_t)thunk);
  int tix;
  if (!cp_integer(cp_uncomment(s), &tix))
    return errh->error("'tickets' takes an integer between 1 and %d", Task::MAX_TICKETS);
  if (tix < 1) {
    errh->warning("tickets pinned at 1");
    tix = 1;
  } else if (tix > Task::MAX_TICKETS) {
    errh->warning("tickets pinned at %d", Task::MAX_TICKETS);
    tix = Task::MAX_TICKETS;
  }
  task->set_tickets(tix);
  return 0;
}
#endif

static String
read_task_scheduled(Element *e, void *thunk)
{
  Task *task = (Task *)((uint8_t *)e + (intptr_t)thunk);
  return String(task->scheduled()) + "\n";
}

#if __MTCLICK__
static String
read_task_home_thread(Element *e, void *thunk)
{
  Task *task = (Task *)((uint8_t *)e + (intptr_t)thunk);
  return String(task->home_thread_id())+String("\n");
}
#endif

/** @brief Register handlers for a task.
 *
 * @param task Task object
 * @param prefix prefix for each handler
 *
 * Adds a standard set of handlers for the task.  They are:
 *
 * @li A "scheduled" read handler, which returns @c true if the task is
 * scheduled and @c false if not.
 * @li A "tickets" read handler, which returns the task's tickets.
 * @li A "tickets" write handler to set the task's tickets.  
 * @li A "home_thread" read handler, which returns the task's home thread ID.
 *
 * Depending on Click's configuration options, some of these handlers might
 * not be available.
 *
 * Each handler name is prefixed with the @a prefix string, so an element with
 * multiple Task objects can register handlers for each of them.
 *
 * @sa add_read_handler, add_write_handler, set_handler
 */
void
Element::add_task_handlers(Task *task, const String &prefix)
{
  intptr_t task_offset = (uint8_t *)task - (uint8_t *)this;
  void *thunk = (void *)task_offset;
  add_read_handler(prefix + "scheduled", read_task_scheduled, thunk);
#ifdef HAVE_STRIDE_SCHED
  add_read_handler(prefix + "tickets", read_task_tickets, thunk);
  add_write_handler(prefix + "tickets", write_task_tickets, thunk);
#endif
#if __MTCLICK__
  add_read_handler(prefix + "home_thread", read_task_home_thread, thunk);
#endif
}

/** @brief Standard read handler returning a positional argument.
 *
 * Use this function to define a handler that returns one of an element's
 * positional configuration arguments.  The @a thunk argument is a typecast
 * integer that specifies which one.  For instance, to add "first", "second",
 * and "third" read handlers that return the element's first three
 * configuration arguments:
 *
 * @code
 * add_read_handler("first", read_positional_handler, (void *) 0);
 * add_read_handler("second", read_positional_handler, (void *) 1);
 * add_read_handler("third", read_positional_handler, (void *) 2);
 * @endcode
 *
 * Returns the empty string if there aren't enough arguments.  Also adds a
 * trailing newline to the returned string if it doesn't end in a newline
 * already.
 *
 * Use read_positional_handler() only for mandatory positional arguments.
 * Optional positional arguments might be polluted by keywords.
 *
 * @sa configuration: used to obtain the element's current configuration.
 * @sa read_keyword_handler, reconfigure_positional_handler, add_read_handler
 */
String
Element::read_positional_handler(Element *element, void *thunk)
{
  Vector<String> conf;
  element->configuration(conf);
  uintptr_t no = (uintptr_t) thunk;
  if (no >= (uintptr_t) conf.size())
    return String();
  String s = conf[no];
  // add trailing "\n" if appropriate
  if (s && s.back() != '\n')
    s += "\n";
  return s;
}

/** @brief Standard read handler returning a keyword argument.
 *
 * Use this function to define a handler that returns one of an element's
 * keyword configuration arguments.  The @a thunk argument is a C string that
 * specifies which one.  For instance, to add a "data" read handler that
 * returns the element's "DATA" keyword argument:
 *
 * @code
 * add_read_handler("data", read_keyword_handler, (void *) "DATA");
 * @endcode
 *
 * Returns the empty string if the configuration doesn't have the specified
 * keyword.  Adds a trailing newline to the returned string if it doesn't end
 * in a newline already.
 *
 * @sa configuration: used to obtain the element's current configuration.
 * @sa read_positional_handler, reconfigure_keyword_handler, add_read_handler
 */
String
Element::read_keyword_handler(Element *element, void *thunk)
{
  Vector<String> conf;
  element->configuration(conf);
  const char *kw = (const char *)thunk;
  String s;
  for (int i = conf.size() - 1; i >= 0; i--)
    if (cp_va_parse_keyword(conf[i], element, ErrorHandler::silent_handler(),
			    kw, cpArgument, &s, cpEnd) > 0)
      break;
  // add trailing "\n" if appropriate
  if (s && s.back() != '\n')
    s += "\n";
  return s;
}

static int
reconfigure_handler(const String &arg, Element *e,
		    int argno, const char *keyword, ErrorHandler *errh)
{
  Vector<String> conf;
  was_default_configuration = 0;
  e->configuration(conf);

  if (keyword) {
    if (was_default_configuration)
      return errh->error("can't use reconfigure_keyword_handler with default configuration() method");
    conf.push_back(String(keyword) + " " + arg);
  } else {
    while (conf.size() <= argno)
      conf.push_back(String());
    conf[argno] = cp_uncomment(arg);
  }

  // create new configuration before calling live_reconfigure(), in case it
  // mucks with the 'conf' array
  String new_config;
  if (keyword)
    new_config = String::stable_string("/* dynamically reconfigured */");
  else
    new_config = cp_unargvec(conf);
  
  if (e->live_reconfigure(conf, errh) < 0)
    return -EINVAL;
  else {
    e->router()->set_default_configuration_string(e->eindex(), new_config);
    return 0;
  }
}

/** @brief Standard write handler for reconfiguring an element by changing one
 * of its positional arguments.
 *
 * Use this function to define a handler that, when written, reconfigures an
 * element by changing one of its positional arguments.  The @a thunk argument
 * is a typecast integer that specifies which one.  For typecast integer that
 * specifies which one.  For instance, to add "first", "second", and "third"
 * write handlers that change the element's first three configuration
 * arguments:
 *
 * @code
 * add_write_handler("first", reconfigure_positional_handler, (void *) 0);
 * add_write_handler("second", reconfigure_positional_handler, (void *) 1);
 * add_write_handler("third", reconfigure_positional_handler, (void *) 2);
 * @endcode
 *
 * When one of these handlers is written, Click will call the element's
 * configuration() method to obtain the element's current configuration,
 * change the relevant argument, and call live_reconfigure() to reconfigure
 * the element.
 *
 * Use reconfigure_positional_handler() only for mandatory positional
 * arguments.  Optional positional arguments might be polluted by keywords.
 *
 * @sa configuration: used to obtain the element's current configuration.
 * @sa live_reconfigure: used to reconfigure the element.
 * @sa reconfigure_keyword_handler, read_positional_handler, add_write_handler
 */
int
Element::reconfigure_positional_handler(const String &arg, Element *e,
					void *thunk, ErrorHandler *errh)
{
  return reconfigure_handler(arg, e, (intptr_t)thunk, 0, errh);
}

/** @brief Standard write handler for reconfiguring an element by changing one
 * of its keyword arguments.
 *
 * Use this function to define a handler that, when written, reconfigures an
 * element by changing one of its keyword arguments.  The @a thunk argument is
 * a C string that specifies which one.  For typecast integer that specifies
 * which one.  For instance, to add a "data" write handler that changes the
 * element's "DATA" configuration argument:
 *
 * @code
 * add_write_handler("data", reconfigure_keyword_handler, (void *) "DATA");
 * @endcode
 *
 * When this handler is written, Click will call the element's configuration()
 * method to obtain the element's current configuration, add the keyword
 * argument to the end (which will generally override any previous
 * occurrences), and call live_reconfigure() to reconfigure the element.
 *
 * reconfigure_keyword_handler() requires the element to provide its own
 * configuration() function, rather than relying on the default.  If you don't
 * override it, all writes to your handler will fail.
 *
 * @sa configuration: used to obtain the element's current configuration.
 * @sa live_reconfigure: used to reconfigure the element.
 * @sa reconfigure_positional_handler, read_keyword_handler, add_write_handler
 */
int
Element::reconfigure_keyword_handler(const String &arg, Element *e,
				     void *thunk, ErrorHandler *errh)
{
  return reconfigure_handler(arg, e, -1, (const char *)thunk, errh);
}

/** @brief Called to handle a low-level remote procedure call.
 *
 * @param command command number
 * @param[in,out] data pointer to any data for the command
 * @return >= 0 on success, < 0 on failure
 *
 * Low-level RPCs are a lightweight mechanism for communicating between
 * user-level programs and a Click kernel module, although they're also
 * available in user-level Click.  Rather than open a file, write ASCII data
 * to the file, and close it, as for handlers, the user-level program calls @c
 * ioctl() on an open file; Click intercepts the @c ioctl and calls the
 * llrpc() method, passing it the @c ioctl number and the associated @a data
 * pointer.  The llrpc() method should read and write @a data as appropriate.
 * @a data may be either a kernel pointer (i.e., directly accessible) or a
 * user pointer (i.e., requires special macros to access), depending on the
 * LLRPC number; see <click/llrpc.h> for more.
 *
 * The return value is returned to the user in @c errno.  Overriding
 * implementations should handle @a commands they understand as appropriate,
 * and call their parents' llrpc() method to handle any other commands.  The
 * default implementation simply returns @c -EINVAL.
 *
 * Click elements should never call each other's llrpc() methods directly; use
 * local_llrpc() instead.
 */
int
Element::llrpc(unsigned command, void *data)
{
    (void) command, (void) data;
    return -EINVAL;
}

/** @brief Execute an LLRPC from within the configuration.
 *
 * @param command command number
 * @param[in,out] data pointer to any data for the command
 *
 * Call this function to execute an element's LLRPC from within another
 * element's code.  It executes any setup code necessary to initialize memory
 * state, then calls llrpc().
 */
int
Element::local_llrpc(unsigned command, void *data)
{
#if CLICK_LINUXMODULE
  mm_segment_t old_fs = get_fs();
  set_fs(get_ds());

  int result = llrpc(command, data);

  set_fs(old_fs);
  return result;
#else
  return llrpc(command, data);
#endif
}

// RUNNING

/** @brief Called to push packet @a p onto push input @a port.
 *
 * @param port the input port number on which the packet arrives
 * @param p the packet
 *
 * An upstream element transferred packet @a p to this element over a push
 * connection.  This element should process the packet as necessary and
 * return.  The packet arrived on input port @a port.  push() must account for
 * the packet either by pushing it further downstream, by freeing it, or by
 * storing it temporarily.
 *
 * The default implementation calls simple_action().
 */
void
Element::push(int port, Packet *p)
{
    (void) port;
    p = simple_action(p);
    if (p)
	output(0).push(p);
}

/** @brief Called to pull a packet from pull output @a port.
 *
 * @param port the output port number receiving the pull request.
 * @return a packet
 *
 * A downstream element initiated a packet transfer from this element over a
 * pull connection.  This element should return a packet pointer, or null if
 * no packet is available.  The pull request arrived on output port @a port.
 *
 * Often, pull() methods will request packets from upstream using
 * input(i).pull().  The default implementation calls simple_action().
 */
Packet *
Element::pull(int port)
{
    (void) port;
    Packet *p = input(0).pull();
    if (p)
	p = simple_action(p);
    return p;
}

/** @brief Called to implement simple packet filters.
 *
 * @param p the input packet
 * @return the output packet, or null
 *
 * Many elements act as simple packet filters: they receive a packet from
 * upstream using input 0, process that packet, and forward it downstream
 * using output 0.  The simple_action() method automates this process.  The @a
 * p argument is the input packet.  simple_action() should process the packet
 * and return a packet pointer -- either the same packet, a different packet,
 * or null.  If the return value isn't null, Click will forward that packet
 * downstream.
 *
 * simple_action() must account for @a p, either by returning it, by freeing
 * it, or by emitting it on some alternate push output port.  (An optional
 * second push output port 1 is often used to emit erroneous packets.)
 *
 * simple_action() works equally well for push or pull port pairs.  The
 * default push() method calls simple_action() this way:
 *
 * @code
 * if ((p = simple_action(p)))
 *     output(0).push(p);
 * @endcode
 *
 * The default pull() method calls it this way instead:
 *
 * @code
 * if (Packet *p = input(0).pull())
 *     if ((p = simple_action(p)))
 *         return p;
 * return 0;
 * @endcode
 *
 * An element that implements its processing with simple_action() should have
 * a processing() code like #AGNOSTIC or "a/ah", and a flow_code() like
 * COMPLETE_FLOW or "x/x" indicating that packets can flow between the first
 * input and the first output.
 *
 * For technical branch prediction-related reasons, elements that use
 * simple_action() can perform quite a bit slower than elements that use
 * push() and pull() directly.  The devirtualizer (click-devirtualize) can
 * mitigate this effect.
 */
Packet *
Element::simple_action(Packet *p)
{
    return p;
}

/** @brief Called to run an element's task.
 *
 * @return true if the task accomplished some meaningful work, false otherwise
 *
 * The Task(Element *) constructor creates a Task object that calls this
 * method when it fires.  Most elements that have tasks use this method.  The
 * default implementation causes an assertion failure.
 */
bool
Element::run_task()
{
    assert(0 /* run_task not overridden */);
    return false;
}

/** @brief Called to run an element's timer.
 *
 * @param timer the timer object that fired
 *
 * The Timer(Element *) constructor creates a Timer object that calls this
 * method when it fires.  Most elements that have timers use this method.
 *
 * @note The default implementation calls the deprecated run_timer() method
 * (the one with no parameters).  In future, the default implementation will
 * cause an assertion failure.
 */
void
Element::run_timer(Timer *timer)
{
    static int nwarn = 0;
    if (nwarn++ < 3)
	click_chatter("warning: calling deprecated run_timer() method;\nreplace with run_timer(Timer *) in your code");
    run_timer();
    (void) timer;
}

/** @brief Called to run an element's timer (deprecated).
 *
 * @deprecated This method is deprecated.  Elements should override the
 * run_timer(Timer *) function instead, which can be used for multiple Timer
 * objects.
 *
 * The Timer(Element *) constructor creates a Timer object that calls this
 * method (via Element::run_timer(Timer *)) when it fires.  The default
 * implementation causes an assertion failure.
 */
void
Element::run_timer()
{
    assert(0 /* run_timer not overridden */);
}

CLICK_ENDDECLS
