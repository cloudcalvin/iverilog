/*
 * Copyright (c) 2002 Stephen Williams (steve@icarus.com)
 *
 *    This source code is free software; you can redistribute it
 *    and/or modify it in source code form under the terms of the GNU
 *    General Public License as published by the Free Software
 *    Foundation; either version 2 of the License, or (at your option)
 *    any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 */
#if !defined(WINNT)
#ident "$Id: net_func.cc,v 1.1 2002/03/09 02:10:22 steve Exp $"
#endif

# include  "netlist.h"
# include  "PExpr.h"

static unsigned count_def_pins(const NetFuncDef*def)
{
      unsigned sum = 0;
      for (unsigned idx = 0 ;  idx < def->port_count() ;  idx += 1)
	    sum += def->port(idx)->pin_count();

      return sum;
}

NetUserFunc::NetUserFunc(NetScope*s, const char*n, NetScope*d)
: NetNode(s, n, count_def_pins(d->func_def())), def_(d)
{
      NetFuncDef*def = def_->func_def();

      unsigned port_wid = port_width(0);
      for (unsigned idx = 0 ;  idx < port_wid ;  idx += 1) {
	    pin(idx).set_dir(Link::OUTPUT);
	    pin(idx).set_name(def_->basename(), idx);
      }

      unsigned pin_base = port_wid;
      for (unsigned idx = 1 ;  idx < port_count() ;  idx += 1) {

	    const NetNet*port_sig = def->port(idx);
	    unsigned bits = port_width(idx);
	    for (unsigned bit = 0; bit < bits; bit += 1) {
		  pin(pin_base+bit).set_dir(Link::INPUT);
		  pin(pin_base+bit).set_name(port_sig->name(), bit);
		  pin(pin_base+bit).drive0(Link::HIGHZ);
		  pin(pin_base+bit).drive1(Link::HIGHZ);
	    }

	    pin_base += bits;
      }
}

NetUserFunc::~NetUserFunc()
{
}

unsigned NetUserFunc::port_count() const
{
      return def_->func_def()->port_count();
}

unsigned NetUserFunc::port_width(unsigned port) const
{
      NetFuncDef*def = def_->func_def();

      assert(port < def->port_count());
      const NetNet*port_sig = def->port(port);

      return port_sig->pin_count();
}

Link& NetUserFunc::port_pin(unsigned port, unsigned idx)
{
      NetFuncDef*def = def_->func_def();
      unsigned pin_base = 0;
      const NetNet*port_sig;

      assert(port < def->port_count());

      for (unsigned port_idx = 0 ;  port_idx < port ;  port_idx += 1) {
	    port_sig = def->port(port_idx);
	    pin_base += port_sig->pin_count();
      }

      port_sig = def->port(port);
      assert(idx < port_sig->pin_count());
      assert((pin_base+idx) < pin_count());

      return pin(pin_base+idx);
}


const NetScope* NetUserFunc::def() const
{
      return def_;
}

/*
 * This method of the PECallFunction class checks that the parameters
 * of the PECallFunction match the function definition. This is used
 * during elaboration to validate the parameters before using them.
 */
bool PECallFunction::check_call_matches_definition_(Design*des, NetScope*dscope) const
{
      assert(dscope);

	/* How many parameters have I got? Normally the size of the
	   list is correct, but there is the special case of a list of
	   1 nil pointer. This is how the parser tells me of no
	   parameter. In other words, ``func()'' is 1 nil parameter. */

      unsigned parms_count = parms_.count();
      if ((parms_count == 1) && (parms_[0] == 0))
	    parms_count = 0;

      if (dscope->type() != NetScope::FUNC) {
	    cerr << get_line() << ": error: Attempt to call scope "
		 << dscope->name() << " as a function." << endl;
	    des->errors += 1;
	    return false;
      }

      if ((parms_count+1) != dscope->func_def()->port_count()) {
	    cerr << get_line() << ": error: Function " << dscope->name()
		 << " expects " << (dscope->func_def()->port_count()-1)
		 << " parameters, you passed " << parms_count << "."
		 << endl;
	    des->errors += 1;
	    return false;
      }

      return true;
}

/*
 * $Log: net_func.cc,v $
 * Revision 1.1  2002/03/09 02:10:22  steve
 *  Add the NetUserFunc netlist node.
 *
 */

