/*
 * Copyright (c) 2001 Stephen Williams (steve@icarus.com)
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
#ident "$Id: compile.cc,v 1.93 2001/08/08 01:05:06 steve Exp $"
#endif

# include  "arith.h"
# include  "bufif.h"
# include  "compile.h"
# include  "functor.h"
# include  "resolv.h"
# include  "udp.h" 
# include  "memory.h" 
# include  "symbols.h"
# include  "codes.h"
# include  "schedule.h"
# include  "vpi_priv.h"
# include  "vthread.h"
# include  "parse_misc.h"
# include  <malloc.h>
# include  <stdlib.h>
# include  <string.h>
# include  <assert.h>

#ifdef __MINGW32__
#include <windows.h>
#endif

unsigned compile_errors = 0;

/*
 * The opcode table lists all the code mnemonics, along with their
 * opcode and operand types. The table is written sorted by mnemonic
 * so that it can be searched by binary search. The opcode_compare
 * function is a helper function for that lookup.
 */

enum operand_e {
	/* Place holder for unused operand */
      OA_NONE,
	/* The operand is a number, an immediate unsigned integer */
      OA_NUMBER,
	/* The operand is a thread bit index */
      OA_BIT1,
      OA_BIT2,
	/* The operand is a pointer to code space */
      OA_CODE_PTR,
	/* The operand is a variable or net pointer */
      OA_FUNC_PTR,
        /* The operand is a pointer to a memory */
      OA_MEM_PTR,
};

struct opcode_table_s {
      const char*mnemonic;
      vvp_code_fun opcode;

      unsigned argc;
      enum operand_e argt[OPERAND_MAX];
};

const static struct opcode_table_s opcode_table[] = {
      { "%add",    of_ADD,    3,  {OA_BIT1,     OA_BIT2,     OA_NUMBER} },
      { "%and",    of_AND,    3,  {OA_BIT1,     OA_BIT2,     OA_NUMBER} },
      { "%and/r",  of_ANDR,   3,  {OA_BIT1,     OA_BIT2,     OA_NUMBER} },
      { "%assign", of_ASSIGN, 3,  {OA_FUNC_PTR, OA_BIT1,     OA_BIT2} },
      { "%assign/m",of_ASSIGN_MEM,3,{OA_MEM_PTR,OA_BIT1,     OA_BIT2} },
      { "%breakpoint", of_BREAKPOINT, 0,  {OA_NONE, OA_NONE, OA_NONE} },
      { "%cmp/s",  of_CMPS,   3,  {OA_BIT1,     OA_BIT2,     OA_NUMBER} },
      { "%cmp/u",  of_CMPU,   3,  {OA_BIT1,     OA_BIT2,     OA_NUMBER} },
      { "%cmp/x",  of_CMPX,   3,  {OA_BIT1,     OA_BIT2,     OA_NUMBER} },
      { "%cmp/z",  of_CMPZ,   3,  {OA_BIT1,     OA_BIT2,     OA_NUMBER} },
      { "%delay",  of_DELAY,  1,  {OA_NUMBER,   OA_NONE,     OA_NONE} },
      { "%delayx", of_DELAYX, 1,  {OA_NUMBER,   OA_NONE,     OA_NONE} },
      { "%end",    of_END,    0,  {OA_NONE,     OA_NONE,     OA_NONE} },
      { "%inv",    of_INV,    2,  {OA_BIT1,     OA_BIT2,     OA_NONE} },
      { "%ix/add", of_IX_ADD, 2,  {OA_BIT1,     OA_NUMBER,   OA_NONE} },
      { "%ix/get", of_IX_GET, 3,  {OA_BIT1,     OA_BIT2,     OA_NUMBER} },
      { "%ix/load",of_IX_LOAD,2,  {OA_BIT1,     OA_NUMBER,   OA_NONE} },
      { "%ix/mul", of_IX_MUL, 2,  {OA_BIT1,     OA_NUMBER,   OA_NONE} },
      { "%ix/sub", of_IX_SUB, 2,  {OA_BIT1,     OA_NUMBER,   OA_NONE} },
      { "%jmp",    of_JMP,    1,  {OA_CODE_PTR, OA_NONE,     OA_NONE} },
      { "%jmp/0",  of_JMP0,   2,  {OA_CODE_PTR, OA_BIT1,     OA_NONE} },
      { "%jmp/0xz",of_JMP0XZ, 2,  {OA_CODE_PTR, OA_BIT1,     OA_NONE} },
      { "%jmp/1",  of_JMP1,   2,  {OA_CODE_PTR, OA_BIT1,     OA_NONE} },
      { "%join",   of_JOIN,   0,  {OA_NONE,     OA_NONE,     OA_NONE} },
      { "%load",   of_LOAD,   2,  {OA_BIT1,     OA_FUNC_PTR, OA_NONE} },
      { "%load/m", of_LOAD_MEM,2, {OA_BIT1,     OA_MEM_PTR,  OA_NONE} },
      { "%load/x", of_LOAD_X, 3,  {OA_BIT1,     OA_FUNC_PTR, OA_BIT2} },
      { "%mod",    of_MOD,    3,  {OA_BIT1,     OA_BIT2,     OA_NUMBER} },
      { "%mov",    of_MOV,    3,  {OA_BIT1,     OA_BIT2,     OA_NUMBER} },
      { "%mul",    of_MUL,    3,  {OA_BIT1,     OA_BIT2,     OA_NUMBER} },
      { "%nand/r", of_NANDR,  3,  {OA_BIT1,     OA_BIT2,     OA_NUMBER} },
      { "%noop",   of_NOOP,   0,  {OA_NONE,     OA_NONE,     OA_NONE} },
      { "%nor/r",  of_NORR,   3,  {OA_BIT1,     OA_BIT2,     OA_NUMBER} },
      { "%or",     of_OR,     3,  {OA_BIT1,     OA_BIT2,     OA_NUMBER} },
      { "%or/r",   of_ORR,    3,  {OA_BIT1,     OA_BIT2,     OA_NUMBER} },
      { "%set",    of_SET,    2,  {OA_FUNC_PTR, OA_BIT1,     OA_NONE} },
      { "%set/m",  of_SET_MEM,2,  {OA_MEM_PTR,  OA_BIT1,     OA_NONE} },
      { "%shiftl/i0", of_SHIFTL_I0, 2, {OA_BIT1,OA_NUMBER,   OA_NONE} },
      { "%shiftr/i0", of_SHIFTR_I0, 2, {OA_BIT1,OA_NUMBER,   OA_NONE} },
      { "%sub",    of_SUB,    3,  {OA_BIT1,     OA_BIT2,     OA_NUMBER} },
      { "%wait",   of_WAIT,   1,  {OA_FUNC_PTR, OA_NONE,     OA_NONE} },
      { "%xnor",   of_XNOR,   3,  {OA_BIT1,     OA_BIT2,     OA_NUMBER} },
      { "%xnor/r", of_XNORR,  3,  {OA_BIT1,     OA_BIT2,     OA_NUMBER} },
      { "%xor",    of_XOR,    3,  {OA_BIT1,     OA_BIT2,     OA_NUMBER} },
      { "%xor/r",  of_XORR,   3,  {OA_BIT1,     OA_BIT2,     OA_NUMBER} },
      { 0, of_NOOP, 0, {OA_NONE, OA_NONE, OA_NONE} }
};

static unsigned opcode_count = 0;
//static const unsigned opcode_count 
//                  = sizeof(opcode_table)/sizeof(*opcode_table) - 1; 
// No?

static int opcode_compare(const void*k, const void*r)
{
      const char*kp = (const char*)k;
      const struct opcode_table_s*rp = (const struct opcode_table_s*)r;
      return strcmp(kp, rp->mnemonic);
}

/*
 * Keep a symbol table of addresses within code space. Labels on
 * executable opcodes are mapped to their address here.
 */
static symbol_table_t sym_codespace = 0;

/*
 * Keep a symbol table of functors mentioned in the source. This table
 * is used to resolve references as they come.
 */
static symbol_table_t sym_functors = 0;

/*
 * VPI objects are indexed during compile time so that they can be
 * linked together as they are created. This symbol table matches
 * labels to vpiHandles.
 */
static symbol_table_t sym_vpi = 0;


/*
 * If a functor parameter makes a forward reference to a functor, then
 * I need to save that reference and resolve it after the functors are
 * created. Use this structure to keep the unresolved references in an
 * unsorted singly linked list.
 *
 * The postpone_functor_input arranges for a functor input to be
 * resolved and connected at cleanup. This is used if the symbol is
 * defined after its use in a functor. The ptr parameter is the
 * complete vvp_input_t for the input port.
 */
struct resolv_list_s {
      struct resolv_list_s*next;
      vvp_ipoint_t port;

      char*source;
      unsigned idx;
};

static struct resolv_list_s*resolv_list = 0;

static void postpone_functor_input(vvp_ipoint_t ptr, char*lab, unsigned idx)
{
      struct resolv_list_s*res = (struct resolv_list_s*)
	    calloc(1, sizeof(struct resolv_list_s));

      res->port   = ptr;
      res->source = lab;
      res->idx    = idx;
      res->next   = resolv_list;
      resolv_list = res;
}


/*
 * Instructions may make forward references to labels or functors. In
 * this case, the reference is short is a llist or flist (depending on
 * the type) and resolved during cleanup.
 */
struct cresolv_list_s {
      struct cresolv_list_s*next;
      struct vvp_code_s*cp;
      char*lab;
      unsigned idx;
};

static struct cresolv_list_s*cresolv_llist = 0;
static struct cresolv_list_s*cresolv_flist = 0;

void compile_vpi_symbol(const char*label, vpiHandle obj)
{
      symbol_value_t val;
      val.ptr = obj;
      sym_set_value(sym_vpi, label, val);
}

/*
 * Initialize the compiler by allocation empty symbol tables and
 * initializing the various address spaces.
 */
void compile_init(void)
{
      sym_vpi = new_symbol_table();
      compile_vpi_symbol("$time", vpip_sim_time());

      sym_functors = new_symbol_table();
      functor_init();

      sym_codespace = new_symbol_table();
      codespace_init();

      opcode_count = 0;
      while (opcode_table[opcode_count].mnemonic)
	    opcode_count += 1;
}

void compile_load_vpi_module(char*name)
{
      vpip_load_module(name);
      free(name);
}

void compile_vpi_time_precision(long pre)
{
      vpip_set_time_precision(pre);
}

/*
 *  Add a functor to the symbol table
 */

static void define_fvector_symbol(char*label, vvp_fvector_t v)
{
      symbol_value_t val;
      val.ptr = v;
      sym_set_value(sym_functors, label, val);
}

static void define_functor_symbol(char*label, vvp_ipoint_t ipt, unsigned wid)
{
      vvp_fvector_t v = vvp_fvector_continuous_new(wid, ipt);
      define_fvector_symbol(label, v);
}

/* 
 * Run through the arguments looking for the functors that are
 * connected to my input ports. For each source functor that I
 * find, connect the output of that functor to the indexed
 * input by inserting myself (complete with the port number in
 * the vvp_ipoint_t) into the list that the source heads.
 *   
 * If the source functor is not declared yet, then don't do
 * the link yet. Save the reference to be resolved later.
 *
 * If the source is a constant value, then set the ival of the functor
 * and skip the symbol lookup.
 */

static void inputs_connect(vvp_ipoint_t fdx, unsigned argc, struct symb_s*argv)
{

      for (unsigned idx = 0;  idx < argc;  idx += 1) {

	      /* Find the functor for this input. This assumes that
		 wide (more then 4 inputs) gates are consecutive
		 functors. */
	    vvp_ipoint_t ifdx = ipoint_input_index(fdx, idx);
	    functor_t iobj = functor_index(ifdx);

	    if (strcmp(argv[idx].text, "C<0>") == 0) {
		  free(argv[idx].text);
		  functor_put_input(iobj, idx, 0, St0);
		  continue;
	    }

	    if (strcmp(argv[idx].text, "C<pu0>") == 0) {
		  free(argv[idx].text);
		  functor_put_input(iobj, idx, 0, Pu0);
		  continue;
	    }

	    if (strcmp(argv[idx].text, "C<su0>") == 0) {
		  free(argv[idx].text);
		  functor_put_input(iobj, idx, 0, Su0);
		  continue;
	    }

	    if (strcmp(argv[idx].text, "C<1>") == 0) {
		  free(argv[idx].text);
		  functor_put_input(iobj, idx, 1, St1);
		  continue;
	    }

	    if (strcmp(argv[idx].text, "C<pu1>") == 0) {
		  free(argv[idx].text);
		  functor_put_input(iobj, idx, 1, Pu1);
		  continue;
	    }

	    if (strcmp(argv[idx].text, "C<su1>") == 0) {
		  free(argv[idx].text);
		  functor_put_input(iobj, idx, 1, Su1);
		  continue;
	    }

	    if (strcmp(argv[idx].text, "C<x>") == 0) {
		  free(argv[idx].text);
		  functor_put_input(iobj, idx, 2, StX);
		  continue;
	    }

	    if (strcmp(argv[idx].text, "C<z>") == 0) {
		  free(argv[idx].text);
		  functor_put_input(iobj, idx, 3, HiZ);
		  continue;
	    }

	    symbol_value_t val = sym_get_value(sym_functors, argv[idx].text);
	    vvp_fvector_t vec = (vvp_fvector_t) val.ptr;

	    if (vec) {
		  vvp_ipoint_t tmp = vvp_fvector_get(vec, argv[idx].idx);
		  functor_t fport = functor_index(tmp);
		  iobj->port[ipoint_port(ifdx)] = fport->out;
		  fport->out = ifdx;
		  free(argv[idx].text);
	    } else {
		  postpone_functor_input(ifdx, argv[idx].text, argv[idx].idx);
	    }
      }
}

/*
 * The parser calls this function to create a functor. I allocate a
 * functor, and map the name to the vvp_ipoint_t address for the
 * functor. Also resolve the inputs to the functor.
 */
void compile_functor(char*label, char*type, unsigned argc, struct symb_s*argv)
{
      vvp_ipoint_t fdx = functor_allocate(1);
      functor_t obj = functor_index(fdx);

      define_functor_symbol(label, fdx, 1);

      assert(argc <= 4);

      obj->ival = 0xaa;
      obj->oval = 2;
      obj->odrive0 = 6;
      obj->odrive1 = 6;
      obj->mode = 0;
#if defined(WITH_DEBUG)
      obj->breakpoint = 0;
#endif

      if (strcmp(type, "OR") == 0) {
	    obj->table = ft_OR;

      } else if (strcmp(type, "AND") == 0) {
	    obj->table = ft_AND;

      } else if (strcmp(type, "BUF") == 0) {
	    obj->table = ft_BUF;

      } else if (strcmp(type, "BUFIF0") == 0) {
	    obj->obj = new vvp_bufif0_s;
	    obj->mode = M42;

      } else if (strcmp(type, "BUFIF1") == 0) {
	    obj->obj = new vvp_bufif1_s;
	    obj->mode = M42;

      } else if (strcmp(type, "MUXZ") == 0) {
	    obj->table = ft_MUXZ;

      } else if (strcmp(type, "EEQ") == 0) {
	    obj->table = ft_EEQ;

      } else if (strcmp(type, "NAND") == 0) {
	    obj->table = ft_NAND;

      } else if (strcmp(type, "NOR") == 0) {
	    obj->table = ft_NOR;

      } else if (strcmp(type, "NOT") == 0) {
	    obj->table = ft_NOT;

      } else if (strcmp(type, "XNOR") == 0) {
	    obj->table = ft_XNOR;

      } else if (strcmp(type, "XOR") == 0) {
	    obj->table = ft_XOR;

      } else {
	    yyerror("invalid functor type.");
      }

	/* Connect the inputs of this functor to the given symbols. If
	   there are C<X> inputs, set the ival appropriately. */
      inputs_connect(fdx, argc, argv);
      free(argv);

	/* Recalculate the output based on the given ival. if the oval
	   turns out to *not* be x, then schedule the functor so that
	   the value gets propagated. */
      unsigned char out = obj->table[obj->ival >> 2];
      obj->oval = 3 & (out >> 2 * (obj->ival&3));
      if (obj->oval != 2)
	    schedule_functor(fdx, 0);

      free(label);
      free(type);
}

static void connect_arith_inputs(vvp_ipoint_t fdx, long wid,
				 vvp_arith_* arith,
				 unsigned argc, struct symb_s*argv)
{
      unsigned opcount = argc / wid;

      struct symb_s tmp_argv[4];
      for (int idx = 0 ;  idx < wid ;  idx += 1) {
	    vvp_ipoint_t ptr = ipoint_index(fdx,idx);
	    functor_t obj = functor_index(ptr);

	    obj->ival = 0xaa >> 2*(4 - opcount);
	    obj->oval = 2;
	    obj->odrive0 = 6;
	    obj->odrive1 = 6;
	    obj->mode = M42;
	    obj->obj  = arith;
#if defined(WITH_DEBUG)
	    obj->breakpoint = 0;
#endif

	    for (unsigned cdx = 0 ;  cdx < opcount ;  cdx += 1)
		  tmp_argv[cdx] = argv[idx + wid*cdx];

	    inputs_connect(ptr, opcount, tmp_argv);
      }

      free(argv);
}

void compile_arith_mult(char*label, long wid,
			unsigned argc, struct symb_s*argv)
{
      assert( wid > 0 );

      if ((long)argc != 2*wid) {
	    fprintf(stderr, "%s; .arith has wrong number of symbols\n", label);
	    compile_errors += 1;
	    free(label);
	    return;
      }

      vvp_ipoint_t fdx = functor_allocate(wid);
      define_functor_symbol(label, fdx, wid);

      vvp_arith_mult*arith = new vvp_arith_mult(fdx, wid);

      connect_arith_inputs(fdx, wid, arith, argc, argv);
}

void compile_arith_sub(char*label, long wid, unsigned argc, struct symb_s*argv)
{
      assert( wid > 0 );

      if ((argc % wid) != 0) {
	    fprintf(stderr, "%s; .arith has wrong number of symbols\n", label);
	    compile_errors += 1;
	    free(label);
	    return;
      }

      unsigned opcount = argc / wid;
      if (opcount > 4) {
	    fprintf(stderr, "%s; .arith has too many operands.\n", label);
	    compile_errors += 1;
	    free(label);
	    return;
      }

      vvp_ipoint_t fdx = functor_allocate(wid);
      define_functor_symbol(label, fdx, wid);

      vvp_arith_sub*arith = new vvp_arith_sub(fdx, wid);

      connect_arith_inputs(fdx, wid, arith, argc, argv);
}

void compile_arith_sum(char*label, long wid, unsigned argc, struct symb_s*argv)
{
      assert( wid > 0 );

      if ((argc % wid) != 0) {
	    fprintf(stderr, "%s; .arith has wrong number of symbols\n", label);
	    compile_errors += 1;
	    free(label);
	    return;
      }

      unsigned opcount = argc / wid;
      if (opcount > 4) {
	    fprintf(stderr, "%s; .arith has too many operands.\n", label);
	    compile_errors += 1;
	    free(label);
	    return;
      }

      vvp_ipoint_t fdx = functor_allocate(wid);
      define_functor_symbol(label, fdx, wid);

      vvp_arith_sum*arith = new vvp_arith_sum(fdx, wid);

      connect_arith_inputs(fdx, wid, arith, argc, argv);
}

void compile_cmp_ge(char*label, long wid, unsigned argc, struct symb_s*argv)
{
      assert( wid > 0 );

      if ((long)argc != 2*wid) {
	    fprintf(stderr, "%s; .cmp has wrong number of symbols\n", label);
	    compile_errors += 1;
	    free(label);
	    return;
      }

      vvp_ipoint_t fdx = functor_allocate(wid);
      define_functor_symbol(label, fdx, wid);

      vvp_cmp_ge*cmp = new vvp_cmp_ge(fdx, wid);

      connect_arith_inputs(fdx, wid, cmp, argc, argv);
}

void compile_cmp_gt(char*label, long wid, unsigned argc, struct symb_s*argv)
{
      assert( wid > 0 );

      if ((long)argc != 2*wid) {
	    fprintf(stderr, "%s; .cmp has wrong number of symbols\n", label);
	    compile_errors += 1;
	    free(label);
	    return;
      }

      vvp_ipoint_t fdx = functor_allocate(wid);
      define_functor_symbol(label, fdx, wid);

      vvp_cmp_gt*cmp = new vvp_cmp_gt(fdx, wid);

      connect_arith_inputs(fdx, wid, cmp, argc, argv);
}

static void compile_shift_inputs(vvp_arith_*dev, vvp_ipoint_t fdx,
				 long wid, unsigned argc, struct symb_s*argv)
{
      for (int idx = 0 ;  idx < wid ;  idx += 1) {
	    vvp_ipoint_t ptr = ipoint_index(fdx,idx);
	    functor_t obj = functor_index(ptr);

	    if ((wid+idx) >= (long)argc)
		  obj->ival = 0x02;
	    else
		  obj->ival = 0x0a;

	    obj->oval = 2;
	    obj->odrive0 = 6;
	    obj->odrive1 = 6;
	    obj->mode = M42;
	    obj->obj  = dev;
#if defined(WITH_DEBUG)
	    obj->breakpoint = 0;
#endif

	    struct symb_s tmp_argv[3];
	    unsigned tmp_argc = 1;
	    tmp_argv[0] = argv[idx];
	    if ((wid+idx) < (long)argc) {
		  tmp_argv[1] = argv[wid+idx];
		  tmp_argc += 1;
	    }

	    inputs_connect(ptr, tmp_argc, tmp_argv);
      }
}

/*
 * A .shift/l statement creates an array of functors for the
 * width. The 0 input is the data vector to be shifted and the 1 input
 * is the amount of the shift. An unconnected shift amount is set to 0.
 */
void compile_shiftl(char*label, long wid, unsigned argc, struct symb_s*argv)
{
      assert( wid > 0 );

      if ((long)argc < (wid+1)) {
	    fprintf(stderr, "%s; .shift/l has too few symbols\n", label);
	    compile_errors += 1;
	    free(label);
	    return;
      }

      if ((long)argc > (wid*2)) {
	    fprintf(stderr, "%s; .shift/l has too many symbols\n", label);
	    compile_errors += 1;
	    free(label);
	    return;
      }

      vvp_ipoint_t fdx = functor_allocate(wid);
      define_functor_symbol(label, fdx, wid);

      vvp_shiftl*dev = new vvp_shiftl(fdx, wid);

      compile_shift_inputs(dev, fdx, wid, argc, argv);

      free(argv);
}

void compile_shiftr(char*label, long wid, unsigned argc, struct symb_s*argv)
{
      assert( wid > 0 );

      if ((long)argc < (wid+1)) {
	    fprintf(stderr, "%s; .shift/r has too few symbols\n", label);
	    compile_errors += 1;
	    free(label);
	    return;
      }

      if ((long)argc > (wid*2)) {
	    fprintf(stderr, "%s; .shift/r has too many symbols\n", label);
	    compile_errors += 1;
	    free(label);
	    return;
      }

      vvp_ipoint_t fdx = functor_allocate(wid);
      define_functor_symbol(label, fdx, wid);

      vvp_shiftr*dev = new vvp_shiftr(fdx, wid);

      compile_shift_inputs(dev, fdx, wid, argc, argv);

      free(argv);
}

void compile_resolver(char*label, char*type, unsigned argc, struct symb_s*argv)
{
      vvp_ipoint_t fdx = functor_allocate(1);
      functor_t obj = functor_index(fdx);

      define_functor_symbol(label, fdx, 1);

      assert(argc <= 4);

      obj->ival = 0xaa;
      obj->oval = 2;
      obj->odrive0 = 6;
      obj->odrive1 = 6;
      obj->mode = M42;
#if defined(WITH_DEBUG)
      obj->breakpoint = 0;
#endif

      if (strcmp(type,"tri") == 0) {
	    obj->obj = new vvp_resolv_s;

      } else {
	    fprintf(stderr, "invalid resolver type: %s\n", type);
	    compile_errors += 1;
      }

	/* Connect the inputs of this functor to the given symbols. If
	   there are C<X> inputs, set the ival appropriately. */
      inputs_connect(fdx, argc, argv);
      free(argv);

	/* This causes the output value to be set from the existing
	   inputs, and if the output is not x, a propagation event is
	   created. */
      obj->obj->set(fdx, obj, false);

      free(label);
      free(type);
}

void compile_udp_def(int sequ, char *label, char *name,
		     unsigned nin, unsigned init, char **table)
{
  struct vvp_udp_s *u = udp_create(label);
  u->name = name;
  u->sequ = sequ;
  u->nin = nin;
  u->init = init;
  u->compile_table(table);
  free(label);
}

char **compile_udp_table(char **table, char *row)
{
  if (table)
    assert(strlen(*table)==strlen(row));

  char **tt;
  for (tt = table; tt && *tt; tt++);
  int n = (tt-table) + 2;

  table = (char**)realloc(table, n*sizeof(char*));
  table[n-2] = row;
  table[n-1] = 0x0;

  return table;
}

void compile_udp_functor(char*label, char*type,
			 unsigned argc, struct symb_s*argv)
{
  struct vvp_udp_s *u = udp_find(type);
  assert (argc == u->nin);

  int nfun = (argc+3)/4;

  vvp_ipoint_t fdx = functor_allocate(nfun);
  functor_t obj = functor_index(fdx);

  define_functor_symbol(label, fdx, nfun);
  free(label);  

  for (unsigned idx = 0;  idx < argc;  idx += 4) 
    {
      vvp_ipoint_t ifdx = ipoint_input_index(fdx, idx);
      functor_t iobj = functor_index(ifdx);

      iobj->ival = 0xaa;
      iobj->old_ival = obj->ival;
      iobj->oval = u->init;
      iobj->mode = M42;
#if defined(WITH_DEBUG)
      iobj->breakpoint = 0;
#endif
      if (idx)
	{
	  iobj->out = fdx;
	  iobj->obj = 0;
	}
      else
	{
	  iobj->obj = u;
	}
    }

  inputs_connect(fdx, argc, argv);
  free(argv);
}


void compile_memory(char *label, char *name, int msb, int lsb,
		    unsigned idxs, long *idx)
{
  vvp_memory_t mem = memory_create(label);
  memory_new(mem, name, lsb, msb, idxs, idx);

  vpiHandle obj = vpip_make_memory(mem);
  compile_vpi_symbol(label, obj);

  free(label);
}

void compile_memory_port(char *label, char *memid, 
			 unsigned msb, unsigned lsb,
			 unsigned naddr,
			 unsigned argc, struct symb_s *argv)
{
  vvp_memory_t mem = memory_find(memid);
  free(memid);
  assert(mem);

  // This is not a Verilog bit range. 
  // This is a data port bit range. 
  assert (lsb >= 0  &&  lsb<=msb);
  assert (msb < memory_data_width(mem));
  unsigned nbits = msb-lsb+1;

  bool writable = argc >= (naddr + 2 + nbits);

  unsigned nfun = naddr;
  if (writable)
	nfun += 2 + nbits;
  assert(nfun == argc);
  nfun = (nfun+3)/4;
  if (nfun < nbits)
    nfun = nbits;
      
  vvp_ipoint_t ix = functor_allocate(nfun);

  define_functor_symbol(label, ix, nfun);
  free(label);

  inputs_connect(ix, argc, argv);
  free(argv);

  memory_port_new(mem, ix, nbits, lsb, naddr, writable);
}

void compile_memory_init(char *memid, unsigned i, unsigned char val)
{
  static vvp_memory_t mem = 0x0;
  static unsigned idx;
  if (memid)
    {
      mem = memory_find(memid);
      free(memid);
      idx = i/4;
    }
  assert(mem);
  memory_init_nibble(mem, idx, val);
  idx++;
}


void compile_event(char*label, char*type,
		   unsigned argc, struct symb_s*argv)
{
      vvp_ipoint_t fdx = functor_allocate(1);
      functor_t obj = functor_index(fdx);

      define_functor_symbol(label, fdx, 1);

      assert(argc <= 4);

	/* Run through the arguments looking for the functors that are
	   connected to my input ports. For each source functor that I
	   find, connect the output of that functor to the indexed
	   input by inserting myself (complete with the port number in
	   the vvp_ipoint_t) into the list that the source heads.

	   If the source functor is not declared yet, then don't do
	   the link yet. Save the reference to be resolved later. */

      inputs_connect(fdx, argc, argv);
      free(argv);

      obj->ival = 0xaa;
      obj->oval = 2;
      obj->odrive0 = 6;
      obj->odrive0 = 6;
      obj->mode = 1;
      obj->out  = 0;
#if defined(WITH_DEBUG)
      obj->breakpoint = 0;
#endif

      obj->event = (struct vvp_event_s*) malloc(sizeof (struct vvp_event_s));
      obj->event->threads = 0;
      obj->event->ival = obj->ival;

      if (strcmp(type,"posedge") == 0)
	    obj->event->vvp_edge_tab = vvp_edge_posedge;
      else if (strcmp(type,"negedge") == 0)
	    obj->event->vvp_edge_tab = vvp_edge_negedge;
      else if (strcmp(type,"edge") == 0)
	    obj->event->vvp_edge_tab = vvp_edge_anyedge;
      else
	    obj->event->vvp_edge_tab = 0;

      free(type);
      free(label);
}

void compile_named_event(char*label, char*name)
{
      vvp_ipoint_t fdx = functor_allocate(1);
      functor_t obj = functor_index(fdx);

      define_functor_symbol(label, fdx, 1);

      obj->ival = 0xaa;
      obj->oval = 2;
      obj->odrive0 = 6;
      obj->odrive1 = 6;
      obj->mode = 2;
      obj->out  = 0;
#if defined(WITH_DEBUG)
      obj->breakpoint = 0;
#endif

      obj->event = (struct vvp_event_s*) malloc(sizeof (struct vvp_event_s));
      obj->event->threads = 0;
      obj->event->ival = obj->ival;

      free(label);
      free(name);
}

void compile_event_or(char*label, unsigned argc, struct symb_s*argv)
{
      vvp_ipoint_t fdx = functor_allocate(1);
      functor_t obj = functor_index(fdx);

      define_functor_symbol(label, fdx, 1);

      obj->ival = 0xaa;
      obj->oval = 2;
      obj->odrive0 = 6;
      obj->odrive1 = 6;
      obj->mode = 2;
      obj->out  = 0;
#if defined(WITH_DEBUG)
      obj->breakpoint = 0;
#endif

      obj->event = new struct vvp_event_s;
      obj->event->threads = 0;
      obj->event->ival = obj->ival;
      obj->event->vvp_edge_tab = 0;

	/* Link the outputs of the named events to me. */

      for (unsigned idx = 0 ;  idx < argc ;  idx += 1) {
	    symbol_value_t val = sym_get_value(sym_functors, argv[idx].text);
	    vvp_fvector_t vec = (vvp_fvector_t) val.ptr;
	    assert(vec);
	    vvp_ipoint_t tmp = vvp_fvector_get(vec, argv[idx].idx);
	    
	    functor_t fport = functor_index(tmp);
	    assert(fport);
	    assert(fport->out == 0);
	    fport->out = fdx;
	    
	    free(argv[idx].text);

      }

      free(argv);
      free(label);
}

/*
 * The parser uses this function to compile an link an executable
 * opcode. I do this by looking up the opcode in the opcode_table. The
 * table gives the operand structure that is acceptible, so I can
 * process the operands here as well.
 */
void compile_code(char*label, char*mnem, comp_operands_t opa)
{
      vvp_cpoint_t ptr = codespace_allocate();

	/* First, I can give the label a value that is the current
	   codespace pointer. Don't need the text of the label after
	   this is done. */
      if (label) {
	    symbol_value_t val;
	    val.num = ptr;
	    sym_set_value(sym_codespace, label, val);
	    free(label);
      }

	/* Lookup the opcode in the opcode table. */
      struct opcode_table_s*op = (struct opcode_table_s*)
	    bsearch(mnem, opcode_table, opcode_count,
		    sizeof(struct opcode_table_s), &opcode_compare);
      if (op == 0) {
	    yyerror("Invalid opcode");
	    return;
      }

      assert(op);

	/* Build up the code from the information about the opcode and
	   the information from the compiler. */
      vvp_code_t code = codespace_index(ptr);
      code->opcode = op->opcode;

      if (op->argc != (opa? opa->argc : 0)) {
	    yyerror("operand count");
	    return;
      }

	/* Pull the operands that the instruction expects from the
	   list that the parser supplied. */

      for (unsigned idx = 0 ;  idx < op->argc ;  idx += 1) {
	    symbol_value_t tmp;

	    switch (op->argt[idx]) {
		case OA_NONE:
		  break;

		case OA_BIT1:
		  if (opa->argv[idx].ltype != L_NUMB) {
			yyerror("operand format");
			break;
		  }

		  code->bit_idx1 = opa->argv[idx].numb;
		  break;

		case OA_BIT2:
		  if (opa->argv[idx].ltype != L_NUMB) {
			yyerror("operand format");
			break;
		  }

		  code->bit_idx2 = opa->argv[idx].numb;
		  break;

		case OA_CODE_PTR:
		  if (opa->argv[idx].ltype != L_SYMB) {
			yyerror("operand format");
			break;
		  }

		  assert(opa->argv[idx].symb.idx == 0);
		  tmp = sym_get_value(sym_codespace, opa->argv[idx].symb.text);
		  code->cptr = tmp.num;
		  if (code->cptr == 0) {
			struct cresolv_list_s*res = (struct cresolv_list_s*)
			      calloc(1, sizeof(struct cresolv_list_s));
			res->cp = code;
			res->lab = opa->argv[idx].symb.text;
			res->next = cresolv_llist;
			cresolv_llist = res;

		  } else {

			free(opa->argv[idx].symb.text);
		  }

		  break;

		case OA_FUNC_PTR:
		    /* The operand is a functor. Resolve the label to
		       a functor pointer, or postpone the resolution
		       if it is not defined yet. */
		  if (opa->argv[idx].ltype != L_SYMB) {
			yyerror("operand format");
			break;
		  }

		  tmp = sym_get_value(sym_functors, opa->argv[idx].symb.text);
		  if (tmp.ptr == 0) {
			struct cresolv_list_s*res = new struct cresolv_list_s;
			res->cp  = code;
			res->lab = opa->argv[idx].symb.text;
			res->idx = opa->argv[idx].symb.idx;
			res->next = cresolv_flist;
			cresolv_flist = res;
		  } else {
			vvp_fvector_t v = (vvp_fvector_t) tmp.ptr;
			code->iptr = 
			      vvp_fvector_get(v, opa->argv[idx].symb.idx);
			free(opa->argv[idx].symb.text);
		  }
		  break;

		case OA_NUMBER:
		  if (opa->argv[idx].ltype != L_NUMB) {
			yyerror("operand format");
			break;
		  }

		  code->number = opa->argv[idx].numb;
		  break;

        	case OA_MEM_PTR:
		  if (opa->argv[idx].ltype != L_SYMB) {
			yyerror("operand format");
			break;
		  }

		  code->mem = memory_find(opa->argv[idx].symb.text);
		  if (code->mem == 0) {
			yyerror("memory undefined");
		  }

		  free(opa->argv[idx].symb.text);
		  break;

	    }
      }

      if (opa) free(opa);

      free(mnem);
}

void compile_codelabel(char*label)
{
      symbol_value_t val;
      vvp_cpoint_t ptr = codespace_next();

      val.num = ptr;
      sym_set_value(sym_codespace, label, val);

      free(label);
}


void compile_disable(char*label, struct symb_s symb)
{
      vvp_cpoint_t ptr = codespace_allocate();

	/* First, I can give the label a value that is the current
	   codespace pointer. Don't need the text of the label after
	   this is done. */
      if (label) {
	    symbol_value_t val;
	    val.num = ptr;
	    sym_set_value(sym_codespace, label, val);
      }


	/* Fill in the basics of the %disable in the instruction. */
      vvp_code_t code = codespace_index(ptr);
      code->opcode = of_DISABLE;

      compile_vpi_lookup(&code->handle, symb.text);

      free(label);
}

/*
 * The %fork instruction is a little different from other instructions
 * in that it has an extended field that holds the information needed
 * to create the new thread. This includes the target PC and scope.
 * I get these from the parser in the form of symbols.
 */
void compile_fork(char*label, struct symb_s dest, struct symb_s scope)
{
      symbol_value_t tmp;
      vvp_cpoint_t ptr = codespace_allocate();

	/* First, I can give the label a value that is the current
	   codespace pointer. Don't need the text of the label after
	   this is done. */
      if (label) {
	    symbol_value_t val;
	    val.num = ptr;
	    sym_set_value(sym_codespace, label, val);
      }

	/* Fill in the basics of the %fork in the instruction. */
      vvp_code_t code = codespace_index(ptr);
      code->opcode = of_FORK;
      code->fork = new struct fork_extend;

	/* Figure out the target PC. */
      tmp = sym_get_value(sym_codespace, dest.text);
      code->fork->cptr = tmp.num;
      if (code->fork->cptr == 0) {
	    struct cresolv_list_s*res = new cresolv_list_s;
	    res->cp = code;
	    res->lab = dest.text;
	    res->next = cresolv_llist;
	    cresolv_llist = res;
	    dest.text = 0;
      }

	/* Figure out the target SCOPE. */
      compile_vpi_lookup((vpiHandle*)&code->fork->scope, scope.text);
      
      free(label);
      free(dest.text);
}

void compile_vpi_call(char*label, char*name, unsigned argc, vpiHandle*argv)
{
      vvp_cpoint_t ptr = codespace_allocate();

	/* First, I can give the label a value that is the current
	   codespace pointer. Don't need the text of the label after
	   this is done. */
      if (label) {
	    symbol_value_t val;
	    val.num = ptr;
	    sym_set_value(sym_codespace, label, val);
	    free(label);
      }

	/* Create an instruction in the code space. */
      vvp_code_t code = codespace_index(ptr);
      code->opcode = &of_VPI_CALL;

	/* Create a vpiHandle that bundles the call information, and
	   store that handle in the instruction. */
      code->handle = vpip_build_vpi_call(name, 0, 0, argc, argv);
      if (code->handle == 0)
	    compile_errors += 1;

	/* Done with the lexor-allocated name string. */
      free(name);
}

void compile_vpi_func_call(char*label, char*name,
			   unsigned vbit, unsigned vwid,
			   unsigned argc, vpiHandle*argv)
{
      vvp_cpoint_t ptr = codespace_allocate();

	/* First, I can give the label a value that is the current
	   codespace pointer. Don't need the text of the label after
	   this is done. */
      if (label) {
	    symbol_value_t val;
	    val.num = ptr;
	    sym_set_value(sym_codespace, label, val);
	    free(label);
      }

	/* Create an instruction in the code space. */
      vvp_code_t code = codespace_index(ptr);
      code->opcode = &of_VPI_CALL;

	/* Create a vpiHandle that bundles the call information, and
	   store that handle in the instruction. */
      code->handle = vpip_build_vpi_call(name, vbit, vwid, argc, argv);
      if (code->handle == 0)
	    compile_errors += 1;

	/* Done with the lexor-allocated name string. */
      free(name);
}

/*
 * When the parser finds a thread statement, I create a new thread
 * with the start address referenced by the program symbol passed to
 * me.
 */
void compile_thread(char*start_sym)
{
      symbol_value_t tmp = sym_get_value(sym_codespace, start_sym);
      vvp_cpoint_t pc = tmp.num;
      if (pc == 0) {
	    yyerror("unresolved address");
	    return;
      }

      vthread_t thr = vthread_new(pc, vpip_peek_current_scope());
      schedule_vthread(thr, 0);
      free(start_sym);
}


struct postponed_handles_list_s {
      struct postponed_handles_list_s*next;
      vpiHandle *handle;
      char*name;
};

static struct postponed_handles_list_s *late_handles;

void compile_vpi_lookup(vpiHandle *handle, char*label)
{
      symbol_value_t val;

      val = sym_get_value(sym_vpi, label);
      if (!val.ptr) {
	    // check for thread vector  T<base,wid>
	    unsigned base, wid;
	    unsigned n;
	    if (2 <= sscanf(label, "T<%u,%u>%n", &base, &wid, &n) 
		&& n == strlen(label)) {
		  val.ptr = vpip_make_vthr_vector(base, wid);
		  sym_set_value(sym_vpi, label, val);
	    }
      }

      if (!val.ptr) {
	    // check for memory word  M<mem,base,wid>
      }

      if (!val.ptr) {
	    struct postponed_handles_list_s*res = 
		  (struct postponed_handles_list_s*)
		  malloc(sizeof(struct postponed_handles_list_s));
	    
	    res->handle  = handle;
	    res->name    = label;
	    res->next    = late_handles;
	    late_handles = res;
      } else {
	    free(label);
      }

      *handle = (vpiHandle) val.ptr;
}

/*
 * A variable is a special functor, so we allocate that functor and
 * write the label into the symbol table.
 */
void compile_variable(char*label, char*name, int msb, int lsb,
		      bool signed_flag)
{
      unsigned wid = ((msb > lsb)? msb-lsb : lsb-msb) + 1;
      vvp_ipoint_t fdx = functor_allocate(wid);

      vvp_fvector_t vec = vvp_fvector_continuous_new(wid, fdx);
      define_fvector_symbol(label, vec);

      for (unsigned idx = 0 ;  idx < wid ;  idx += 1) {
	    functor_t obj = functor_index(ipoint_index(fdx,idx));
	    obj->table = ft_var;
	    obj->ival  = 0x22;
	    obj->oval  = 0x02;
	    obj->odrive0 = 6;
	    obj->odrive1 = 6;
	    obj->mode  = 0;
#if defined(WITH_DEBUG)
	    obj->breakpoint = 0;
#endif
      }

	/* Make the vpiHandle for the reg. */
      vpiHandle obj = vpip_make_reg(name, msb, lsb, signed_flag, vec);
      compile_vpi_symbol(label, obj);
      vpip_attach_to_current_scope(obj);

      free(label);
}

void compile_net(char*label, char*name, int msb, int lsb, bool signed_flag,
		 unsigned argc, struct symb_s*argv)
{
      unsigned wid = ((msb > lsb)? msb-lsb : lsb-msb) + 1;
      vvp_ipoint_t fdx = functor_allocate(wid);

      vvp_fvector_t vec = vvp_fvector_continuous_new(wid, fdx);
      define_fvector_symbol(label, vec);

	/* Allocate all the functors for the net itself. */
      for (unsigned idx = 0 ;  idx < wid ;  idx += 1) {
	    functor_t obj = functor_index(ipoint_index(fdx,idx));
	    obj->table = ft_var;
	    obj->ival  = 0x02;
	    obj->oval  = 0x02;
	    obj->odrive0 = 6;
	    obj->odrive1 = 6;
	    obj->mode  = 0;
#if defined(WITH_DEBUG)
	    obj->breakpoint = 0;
#endif
      }

      assert(argc == wid);

	/* Connect port[0] of each of the net functors to the output
	   of the addressed object. */
      for (unsigned idx = 0 ;  idx < wid ;  idx += 1) {
	    vvp_ipoint_t ptr = ipoint_index(fdx,idx);
	    functor_t obj = functor_index(ptr);

	      /* Skip unconnected nets. */
	    if (argv[idx].text == 0) {
		  obj->oval = 3;
		  continue;
	    }

	    if (strcmp(argv[idx].text, "C<0>") == 0) {
		  obj->oval = 0;
		  schedule_functor(ptr, 0);
		  continue;
	    }

	    if (strcmp(argv[idx].text, "C<su0>") == 0) {
		  obj->oval = 0;
		  obj->odrive0 = 7;
		  obj->odrive1 = 7;
		  schedule_functor(ptr, 0);
		  continue;
	    }

	    if (strcmp(argv[idx].text, "C<1>") == 0) {
		  obj->oval = 1;
		  schedule_functor(ptr, 0);
		  continue;
	    }

	    if (strcmp(argv[idx].text, "C<su1>") == 0) {
		  obj->oval = 1;
		  obj->odrive0 = 7;
		  obj->odrive1 = 7;
		  schedule_functor(ptr, 0);
		  continue;
	    }

	    if (strcmp(argv[idx].text, "C<x>") == 0) {
		  obj->oval = 2;
		  continue;
	    }

	    if (strcmp(argv[idx].text, "C<z>") == 0) {
		  obj->oval = 3;
		  schedule_functor(ptr, 0);
		  continue;
	    }

	    symbol_value_t val = sym_get_value(sym_functors, argv[idx].text);
	    if (val.ptr) {
		  vvp_fvector_t vec = (vvp_fvector_t) val.ptr;
		  functor_t src =
			functor_index(vvp_fvector_get(vec, argv[idx].idx));
		  obj->port[0] = src->out;
		  src->out = ptr;

	    } else {
		  postpone_functor_input(ipoint_make(ptr, 0),
					 argv[idx].text,
					 argv[idx].idx);
	    }
      }

	/* Make the vpiHandle for the reg. */
      vpiHandle obj = vpip_make_net(name, msb, lsb, signed_flag, vec);
      compile_vpi_symbol(label, obj);
      vpip_attach_to_current_scope(obj);

      free(label);
      free(argv);
}

/*
 * When parsing is otherwise complete, this function is called to do
 * the final stuff. Clean up deferred linking here.
 */
void compile_cleanup(void)
{
      struct resolv_list_s*tmp_list = resolv_list;
      resolv_list = 0;

      while (tmp_list) {
	    struct resolv_list_s*res = tmp_list;
	    tmp_list = res->next;

	      /* Get the addressed functor object and select the input
		 port that needs resolution. */
	    functor_t obj = functor_index(res->port);
	    unsigned idx = ipoint_port(res->port);

	      /* Try again to look up the symbol that was not defined
		 the first time around. */
	    symbol_value_t val = sym_get_value(sym_functors, res->source);
	    vvp_fvector_t vec = (vvp_fvector_t) val.ptr;

	    if (vec != 0) {
		    /* The symbol is defined, link the functor input
		       to the resolved output. */

		  vvp_ipoint_t tmp = vvp_fvector_get(vec, res->idx);
		  functor_t fport = functor_index(tmp);
		  obj->port[idx] = fport->out;
		  fport->out = res->port;

		  free(res->source);
		  free(res);

	    } else {
		    /* Still not resolved. put back into the list. */
		  fprintf(stderr, "unresolved functor reference: %s\n",
			  res->source);
		  res->next = resolv_list;
		  resolv_list = res;
		  compile_errors += 1;
	    }
      }

      struct cresolv_list_s*tmp_clist = cresolv_llist;
      cresolv_llist = 0;

      while (tmp_clist) {
	    struct cresolv_list_s*res = tmp_clist;
	    tmp_clist = res->next;

	    symbol_value_t val = sym_get_value(sym_codespace, res->lab);
	    vvp_cpoint_t tmp = val.num;

	    if (tmp != 0) {
		    /* Resolved the reference. If this is a %fork,
		       then handle it slightly differently. */
		  if (res->cp->opcode == of_FORK)
			res->cp->fork->cptr = tmp;
		  else
			res->cp->cptr = tmp;
		  free(res->lab);
		  
	    } else {
		  compile_errors += 1;
		  fprintf(stderr, "unresolved code label: %s\n", res->lab);
		  res->next = cresolv_llist;
		  cresolv_llist = res;
	    }
      }

      tmp_clist = cresolv_flist;
      cresolv_flist = 0;

      while (tmp_clist) {
	    struct cresolv_list_s*res = tmp_clist;
	    tmp_clist = res->next;

	    symbol_value_t val = sym_get_value(sym_functors, res->lab);
	    vvp_fvector_t vec = (vvp_fvector_t) val.ptr;

	    if (vec != 0) {
		  res->cp->iptr = vvp_fvector_get(vec, res->idx);
		  free(res->lab);
		  
	    } else {
		  compile_errors += 1;
		  fprintf(stderr, "unresolved code reference "
			  "to functor: %s\n", res->lab);
		  res->next = cresolv_llist;
		  cresolv_flist = res;
	    }
      }

      struct postponed_handles_list_s *lhandle = late_handles;
      late_handles = 0x0;
      while (lhandle) {
	    struct postponed_handles_list_s *tmp = lhandle;
	    lhandle = lhandle->next;
	    compile_vpi_lookup(tmp->handle, tmp->name);
	    free(tmp);
      }
      lhandle = late_handles;
      while (lhandle) {
	    compile_errors += 1;
	    fprintf(stderr, 
		    "unresolved vpi name lookup: %s\n",
		    lhandle->name);
	    lhandle = lhandle->next;
      }
}

/*
 * These functions are in support of the debugger.
 *
 * debug_lookup_functor
 *    Use a name to locate a functor address. This only gets the LSB
 *    of a vector, but it is enough to locate the object, or, is it?
 */
vvp_ipoint_t debug_lookup_functor(const char*name)
{
      symbol_value_t val = sym_get_value(sym_functors, name);
      vvp_fvector_t vec = (vvp_fvector_t) val.ptr;
      if (!vec)
	    return 0;
      return vvp_fvector_get(vec, 0);
}


/*
 * $Log: compile.cc,v $
 * Revision 1.93  2001/08/08 01:05:06  steve
 *  Initial implementation of vvp_fvectors.
 *  (Stephan Boettcher)
 *
 * Revision 1.92  2001/07/30 03:53:01  steve
 *  Initialize initial functor tables.
 *
 * Revision 1.91  2001/07/28 03:12:39  steve
 *  Support C<su0> and C<su1> special symbols.
 *
 * Revision 1.90  2001/07/26 03:13:51  steve
 *  Make the -M flag add module search paths.
 *
 * Revision 1.89  2001/07/22 00:04:50  steve
 *  Add the load/x instruction for bit selects.
 *
 * Revision 1.88  2001/07/19 04:40:55  steve
 *  Add support for the delayx opcode.
 *
 * Revision 1.87  2001/07/11 04:43:57  steve
 *  support postpone of $systask parameters. (Stephan Boettcher)
 *
 * Revision 1.86  2001/07/07 02:57:33  steve
 *  Add the .shift/r functor.
 *
 * Revision 1.85  2001/07/06 05:02:43  steve
 *  Properly initialize unconnected shift inputs.
 *
 * Revision 1.84  2001/07/06 04:46:44  steve
 *  Add structural left shift (.shift/l)
 *
 * Revision 1.83  2001/06/30 23:03:16  steve
 *  support fast programming by only writing the bits
 *  that are listed in the input file.
 *
 * Revision 1.82  2001/06/30 21:07:26  steve
 *  Support non-const right shift (unsigned).
 *
 * Revision 1.81  2001/06/23 18:26:26  steve
 *  Add the %shiftl/i0 instruction.
 *
 * Revision 1.80  2001/06/23 01:04:07  steve
 *  Allow forward references of task scopes. (Stephan Boettcher)
 *
 * Revision 1.79  2001/06/19 03:01:10  steve
 *  Add structural EEQ gates (Stephan Boettcher)
 *
 * Revision 1.78  2001/06/18 01:09:32  steve
 *  More behavioral unary reduction operators.
 *  (Stephan Boettcher)
 *
 * Revision 1.77  2001/06/16 23:45:05  steve
 *  Add support for structural multiply in t-dll.
 *  Add code generators and vvp support for both
 *  structural and behavioral multiply.
 *
 * Revision 1.76  2001/06/15 04:07:58  steve
 *  Add .cmp statements for structural comparison.
 *
 * Revision 1.75  2001/06/15 03:28:31  steve
 *  Change the VPI call process so that loaded .vpi modules
 *  use a function table instead of implicit binding.
 *
 * Revision 1.74  2001/06/10 17:12:51  steve
 *  Instructions can forward reference functors.
 *
 * Revision 1.73  2001/06/10 16:47:49  steve
 *  support scan of scope from VPI.
 *
 * Revision 1.72  2001/06/07 03:09:03  steve
 *  Implement .arith/sub subtraction.
 *
 * Revision 1.71  2001/06/05 03:05:41  steve
 *  Add structural addition.
 *
 * Revision 1.70  2001/05/31 04:12:43  steve
 *  Make the bufif0 and bufif1 gates strength aware,
 *  and accurately propagate strengths of outputs.
 *
 * Revision 1.69  2001/05/30 03:02:35  steve
 *  Propagate strength-values instead of drive strengths.
 *
 * Revision 1.68  2001/05/24 04:20:10  steve
 *  Add behavioral modulus.
 *
 * Revision 1.67  2001/05/22 04:08:16  steve
 *  Get the initial inputs to functors set at xxxx.
 *
 * Revision 1.66  2001/05/22 02:14:47  steve
 *  Update the mingw build to not require cygwin files.
 *
 * Revision 1.65  2001/05/20 00:46:12  steve
 *  Add support for system function calls.
 *
 * Revision 1.64  2001/05/13 21:05:06  steve
 *  calculate the output of resolvers.
 *
 * Revision 1.63  2001/05/12 20:38:06  steve
 *  A resolver that understands some simple strengths.
 *
 * Revision 1.62  2001/05/10 00:26:53  steve
 *  VVP support for memories in expressions,
 *  including general support for thread bit
 *  vectors as system task parameters.
 *  (Stephan Boettcher)
 *
 * Revision 1.61  2001/05/09 04:23:18  steve
 *  Now that the interactive debugger exists,
 *  there is no use for the output dump.
 *
 * Revision 1.60  2001/05/09 02:53:25  steve
 *  Implement the .resolv syntax.
 *
 * Revision 1.59  2001/05/08 23:59:33  steve
 *  Add ivl and vvp.tgt support for memories in
 *  expressions and l-values. (Stephan Boettcher)
 *
 * Revision 1.58  2001/05/08 23:32:26  steve
 *  Add to the debugger the ability to view and
 *  break on functors.
 *
 *  Add strengths to functors at compile time,
 *  and Make functors pass their strengths as they
 *  propagate their output.
 *
 * Revision 1.57  2001/05/06 17:42:22  steve
 *  Add the %ix/get instruction. (Stephan Boettcher)
 *
 * Revision 1.56  2001/05/06 03:51:37  steve
 *  Regularize the mode-42 functor handling.
 *
 * Revision 1.55  2001/05/06 00:18:13  steve
 *  Propagate non-x constant net values.
 *
 * Revision 1.54  2001/05/05 23:55:46  steve
 *  Add the beginnings of an interactive debugger.
 *
 * Revision 1.53  2001/05/02 23:16:50  steve
 *  Document memory related opcodes,
 *  parser uses numbv_s structures instead of the
 *  symbv_s and a mess of unions,
 *  Add the %is/sub instruction.
 *        (Stephan Boettcher)
 *
 * Revision 1.52  2001/05/02 04:05:17  steve
 *  Remove the init parameter of functors, and instead use
 *  the special C<?> symbols to initialize inputs. This is
 *  clearer and more regular.
 *
 * Revision 1.51  2001/05/02 01:57:25  steve
 *  Support behavioral subtraction.
 *
 * Revision 1.50  2001/05/01 05:00:02  steve
 *  Implement %ix/load.
 *
 * Revision 1.49  2001/05/01 02:18:15  steve
 *  Account for ipoint_input_index behavior in inputs_connect.
 *
 * Revision 1.48  2001/05/01 01:09:39  steve
 *  Add support for memory objects. (Stephan Boettcher)
 */

