/*******************************************************************\

Module: Function Inlining

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#include <cassert>

#include <util/prefix.h>
#include <util/cprover_prefix.h>
#include <util/base_type.h>
#include <util/std_code.h>
#include <util/std_expr.h>
#include <util/expr_util.h>

#include "remove_skip.h"
#include "goto_inline.h"
#include "goto_inline_class.h"

/*******************************************************************\

Function: goto_inlinet::parameter_assignments

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_inlinet::parameter_assignments(
  const source_locationt &source_location,
  const irep_idt &function_name,
  const code_typet &code_type,
  const exprt::operandst &arguments,
  goto_programt &dest)
{
  // iterates over the operands
  exprt::operandst::const_iterator it1=arguments.begin();

  const code_typet::parameterst &parameter_types=
    code_type.parameters();
  
  // iterates over the types of the parameters
  for(code_typet::parameterst::const_iterator
      it2=parameter_types.begin();
      it2!=parameter_types.end();
      it2++)
  {
    // if you run out of actual arguments there was a mismatch
    if(it1==arguments.end())
    {
      err_location(source_location);
      str << "call to `" << function_name << "': not enough arguments";
      throw 0;
    }

    const code_typet::parametert &parameter=*it2;

    // this is the type the n-th argument should be
    const typet &par_type=ns.follow(parameter.type());

    const irep_idt &identifier=parameter.get_identifier();

    if(identifier==irep_idt())
    {
      err_location(source_location);
      throw "no identifier for function parameter";
    }

    {
      const symbolt &symbol=ns.lookup(identifier);

      goto_programt::targett decl=dest.add_instruction();
      decl->make_decl();
      decl->code=code_declt(symbol.symbol_expr());
      decl->code.add_source_location()=source_location;
      decl->source_location=source_location;
      decl->function=function_name; 
    }

    // nil means "don't assign"
    if(it1->is_nil())
    {    
    }
    else
    {
      // this is the actual parameter
      exprt actual=*it1;

      // it should be the same exact type as the parameter,
      // subject to some exceptions
      if(!base_type_eq(par_type, actual.type(), ns))
      {
        const typet &f_partype = ns.follow(par_type);
        const typet &f_acttype = ns.follow(actual.type());
        
        // we are willing to do some conversion
        if((f_partype.id()==ID_pointer &&
            f_acttype.id()==ID_pointer) ||
           (f_partype.id()==ID_pointer &&
            f_acttype.id()==ID_array &&
            f_partype.subtype()==f_acttype.subtype()))
        {
          actual.make_typecast(par_type);
        }
        else if((f_partype.id()==ID_signedbv ||
                 f_partype.id()==ID_unsignedbv ||
                 f_partype.id()==ID_bool) &&
                (f_acttype.id()==ID_signedbv ||
                 f_acttype.id()==ID_unsignedbv ||
                 f_acttype.id()==ID_bool))  
        {
          actual.make_typecast(par_type);
        }
        else
        {
          err_location(*it1);

          str << "function call: argument `" << identifier
              << "' type mismatch: argument is `"
              // << from_type(ns, identifier, actual.type())
              << actual.type().pretty()
              << "', parameter is `"
              << from_type(ns, identifier, par_type)
              << "'";
          throw 0;
        }
      }

      // adds an assignment of the actual parameter to the formal parameter
      code_assignt assignment(symbol_exprt(identifier, par_type), actual);
      assignment.add_source_location()=source_location;

      dest.add_instruction(ASSIGN);
      dest.instructions.back().source_location=source_location;
      dest.instructions.back().code.swap(assignment);
      dest.instructions.back().function=function_name;      
    }

    it1++;
  }

  if(it1!=arguments.end())
  {
    // too many arguments -- we just ignore that, no harm done
  }
}

/*******************************************************************\

Function: goto_inlinet::parameter_destruction

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_inlinet::parameter_destruction(
  const source_locationt &source_location,
  const irep_idt &function_name,
  const code_typet &code_type,
  goto_programt &dest)
{
  const code_typet::parameterst &parameter_types=
    code_type.parameters();
  
  // iterates over the types of the parameters
  for(code_typet::parameterst::const_iterator
      it=parameter_types.begin();
      it!=parameter_types.end();
      it++)
  {
    const code_typet::parametert &parameter=*it;

    const irep_idt &identifier=parameter.get_identifier();

    if(identifier==irep_idt())
    {
      err_location(source_location);
      throw "no identifier for function parameter";
    }

    {
      const symbolt &symbol=ns.lookup(identifier);

      goto_programt::targett dead=dest.add_instruction();
      dead->make_dead();
      dead->code=code_deadt(symbol.symbol_expr());
      dead->code.add_source_location()=source_location;
      dead->source_location=source_location;
      dead->function=function_name; 
    }
  }
}

/*******************************************************************\

Function: goto_inlinet::replace_return

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_inlinet::replace_return(
  goto_programt &dest,
  const exprt &lhs,
  const exprt &constrain)
{
  for(goto_programt::instructionst::iterator
      it=dest.instructions.begin();
      it!=dest.instructions.end();
      it++)
  {
    if(it->is_return())
    {
      #if 0
      if(lhs.is_not_nil())
      {
        if(it->code.operands().size()!=1)
        {
          err_location(it->code);
          str << "return expects one operand!";
          warning_msg();
          continue;
        }
      
        goto_programt tmp;
        goto_programt::targett assignment=tmp.add_instruction(ASSIGN);
        
        code_assignt code_assign(lhs, it->code.op0());

        // this may happen if the declared return type at the call site
        // differs from the defined return type
        if(code_assign.lhs().type()!=
           code_assign.rhs().type())
          code_assign.rhs().make_typecast(code_assign.lhs().type());

        assignment->code=code_assign;
        assignment->source_location=it->source_location;
        assignment->function=it->function;
        
        if(constrain.is_not_nil() && !constrain.is_true())
        {
          codet constrain(ID_bp_constrain);
          constrain.reserve_operands(2);
          constrain.move_to_operands(assignment->code);
          constrain.copy_to_operands(constrain);
        }
        
        dest.insert_before_swap(it, *assignment);
        it++;
      }
      else if(!it->code.operands().empty())
      {
        goto_programt tmp;
        goto_programt::targett expression=tmp.add_instruction(OTHER);
        
        expression->code=codet(ID_expression);
        expression->code.move_to_operands(it->code.op0());
        expression->source_location=it->source_location;
        expression->function=it->function;
        
        dest.insert_before_swap(it, *expression);
        it++;
      }

      it->make_goto(--dest.instructions.end());
      #else
      if(lhs.is_not_nil())
      {
        if(it->code.operands().size()!=1)
        {
          err_location(it->code);
          str << "return expects one operand!";
          str << '\n' << it->code.pretty();
          warning_msg();
          continue;
        }
      
        code_assignt code_assign(lhs, it->code.op0());

        // this may happen if the declared return type at the call site
        // differs from the defined return type
        if(code_assign.lhs().type()!=
           code_assign.rhs().type())
          code_assign.rhs().make_typecast(code_assign.lhs().type());

        if(constrain.is_not_nil() && !constrain.is_true())
        {
          codet constrain(ID_bp_constrain);
          constrain.reserve_operands(2);
          constrain.move_to_operands(code_assign);
          constrain.copy_to_operands(constrain);
          it->code=constrain;
          it->type=OTHER;
        }
        else
        {
          it->code=code_assign;
          it->type=ASSIGN;
        }

        it++;
      }
      else if(!it->code.operands().empty())
      {
        codet expression(ID_expression);
        expression.move_to_operands(it->code.op0());
        it->code=expression;
        it->type=OTHER;
        it++;
      }
      #endif
    }
  }
}

/*******************************************************************\

Function: replace_location

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void replace_location(
  source_locationt &dest,
  const source_locationt &new_location)
{
  // we copy, and then adjust for property_id, property_class
  // and comment, if necessary

  irep_idt comment=dest.get_comment();
  irep_idt property_class=dest.get_property_class();
  irep_idt property_id=dest.get_property_id();

  dest=new_location;
  
  if(comment!=irep_idt()) dest.set_comment(comment);
  if(property_class!=irep_idt()) dest.set_property_class(property_class);
  if(property_id!=irep_idt()) dest.set_property_id(property_id);
}

/*******************************************************************\

Function: replace_location

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void replace_location(
  exprt &dest,
  const source_locationt &new_location)
{
  Forall_operands(it, dest)
    replace_location(*it, new_location);

  if(dest.find(ID_C_source_location).is_not_nil())
    replace_location(dest.add_source_location(), new_location);
}

/*******************************************************************\

Function: goto_inlinet::expand_function_call

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_inlinet::expand_function_call(
  goto_programt &dest,
  goto_programt::targett &target,
  const exprt &lhs,
  const symbol_exprt &function,
  const exprt::operandst &arguments,
  const exprt &constrain,
  bool full)
{
  // look it up
  const irep_idt identifier=function.get_identifier();
  
  // we ignore certain calls
  if(identifier=="__CPROVER_cleanup" ||
     identifier=="__CPROVER_set_must" ||
     identifier=="__CPROVER_set_may" ||
     identifier=="__CPROVER_clear_must" ||
     identifier=="__CPROVER_clear_may")
  {
    target++;
    return; // ignore
  }
  
  // see if we are already expanding it
  if(recursion_set.find(identifier)!=recursion_set.end())
  {
    if(!full)
    {
      target++;
      return; // simply ignore, we don't do full inlining, it's ok
    }

    // it's really recursive, and we need full inlining.
    // Uh. Buh. Give up.
    err_location(function);
    str << "recursion is ignored";
    warning_msg();
    target->make_skip();
    
    target++;
    return;
  }

  goto_functionst::function_mapt::iterator m_it=
    goto_functions.function_map.find(identifier);

  if(m_it==goto_functions.function_map.end())
  {
    if(!full)
    {
      target++;
      return; // simply ignore, we don't do full inlining, it's ok
    }

    err_location(function);
    str << "failed to find function `" << identifier << "'";
    throw 0;
  }
  
  const goto_functionst::goto_functiont &f=m_it->second;

  // see if we need to inline this  
  if(!full)
  {
    if(!f.body_available() ||
       (!f.is_inlined() && f.body.instructions.size() > smallfunc_limit))
    {
      target++;
      return;
    }
  }

  if(f.body_available())
  {
    recursion_set.insert(identifier);

    // first make sure that this one is already inlined
    goto_inline_rec(m_it, full);

    goto_programt tmp2;
    tmp2.copy_from(f.body);
    
    assert(tmp2.instructions.back().is_end_function());
    tmp2.instructions.back().type=LOCATION;
    
    replace_return(tmp2, lhs, constrain);

    goto_programt tmp;
    parameter_assignments(target->source_location, identifier, f.type, arguments, tmp);
    tmp.destructive_append(tmp2);
    parameter_destruction(target->source_location, identifier, f.type, tmp);

    if(f.is_hidden())
    {
      source_locationt new_source_location=
        function.find_source_location();
    
      if(new_source_location.is_not_nil())
      {
        new_source_location.set_hide();
      
        Forall_goto_program_instructions(it, tmp)
        {
          if(it->function==identifier)
          {
            // don't hide assignment to lhs
            if(it->is_assign() && to_code_assign(it->code).lhs()==lhs)
            {
            }
            else
            {
              replace_location(it->source_location, new_source_location);
              replace_location(it->guard, new_source_location);
              replace_location(it->code, new_source_location);
            }

            it->function=target->function;
          }
        }
      }
    }

    // set up location instruction for function call  
    target->type=LOCATION;
    target->code.clear();
    
    goto_programt::targett next_target(target);
    next_target++;

    dest.instructions.splice(next_target, tmp.instructions);
    target=next_target;

    recursion_set.erase(identifier);
  }
  else // no body available
  {
    if(no_body_set.insert(identifier).second)
    {
      err_location(function);
      str << "no body for function `" << identifier << "'";
      warning_msg();
    }

    goto_programt tmp;

    // evaluate function arguments -- they might have
    // pointer dereferencing or the like
    forall_expr(it, arguments)
    {
      goto_programt::targett t=tmp.add_instruction();
      t->make_other();
      t->source_location=target->source_location;
      t->function=target->function;
      t->code=codet(ID_expression);
      t->code.copy_to_operands(*it);
    }
    
    // return value
    if(lhs.is_not_nil())
    {
      side_effect_expr_nondett rhs(lhs.type());
      rhs.add_source_location()=target->source_location;

      code_assignt code(lhs, rhs);
      code.add_source_location()=target->source_location;
    
      goto_programt::targett t=tmp.add_instruction(ASSIGN);
      t->source_location=target->source_location;
      t->function=target->function;
      t->code.swap(code);
    }

    // now just kill call
    target->type=LOCATION;
    target->code.clear();
    target++;

    // insert tmp
    dest.instructions.splice(target, tmp.instructions);
  }
}

/*******************************************************************\

Function: goto_inlinet::goto_inline

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_inlinet::goto_inline(goto_programt &dest)
{
  goto_inline_rec(dest, true);
  replace_return(dest, 
    static_cast<const exprt &>(get_nil_irep()),
    static_cast<const exprt &>(get_nil_irep()));
}

/*******************************************************************\

Function: goto_inlinet::goto_inline_rec

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_inlinet::goto_inline_rec(
  goto_functionst::function_mapt::iterator f_it,
  bool full)
{
  // already done?
  
  if(finished_inlining_set.find(f_it->first)!=
     finished_inlining_set.end())
    return; // yes
    
  // do it
  
  goto_inline_rec(f_it->second.body, full);
  
  // remember we did it
 
  finished_inlining_set.insert(f_it->first); 
}

/*******************************************************************\

Function: goto_inlinet::goto_inline_rec

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_inlinet::goto_inline_rec(goto_programt &dest, bool full)
{
  bool changed=false;

  for(goto_programt::instructionst::iterator
      it=dest.instructions.begin();
      it!=dest.instructions.end();
      ) // no it++, done by inline_instruction
  {
    if(inline_instruction(dest, full, it))
      changed=true;
  }

  if(changed)
  {
    remove_skip(dest);  
    dest.update();
  }
}

/*******************************************************************\

Function: goto_inlinet::inline_instruction

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool goto_inlinet::inline_instruction(
  goto_programt &dest,
  bool full,
  goto_programt::targett &it)
{
  if(it->is_function_call())
  {
    const code_function_callt &call=to_code_function_call(it->code);

    if(call.function().id()==ID_symbol)
    {
      expand_function_call(
        dest, it, call.lhs(), to_symbol_expr(call.function()),
        call.arguments(),
        static_cast<const exprt &>(get_nil_irep()), full);

      return true;
    }
  }
  else if(it->is_other())
  {
    // these are for Boolean programs
    if(it->code.get(ID_statement)==ID_bp_constrain &&
       it->code.operands().size()==2 &&
       it->code.op0().operands().size()==2 &&
       it->code.op0().op1().get(ID_statement)==ID_function_call)
    {
      expand_function_call(
        dest, it,
        it->code.op0().op0(), // lhs
        to_symbol_expr(it->code.op0().op1().op0()), // function
        it->code.op0().op1().op1().operands(), // arguments
        it->code.op1(), // constraint
        full);

      return true;
    }
  }

  // advance iterator  
  it++;

  return false; 
}

/*******************************************************************\

Function: goto_inline

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_inline(
  goto_functionst &goto_functions,
  const namespacet &ns,
  message_handlert &message_handler)
{
  goto_inlinet goto_inline(goto_functions, ns, message_handler);

  try
  {
    // find entry point
    goto_functionst::function_mapt::iterator it=
      goto_functions.function_map.find(goto_functionst::entry_point());
      
    if(it==goto_functions.function_map.end())
      return;
    
    goto_inline.goto_inline(it->second.body);
  }

  catch(int)
  {
    goto_inline.error_msg();
  }

  catch(const char *e)
  {
    goto_inline.str << e;
    goto_inline.error_msg();
  }

  catch(const std::string &e)
  {
    goto_inline.str << e;
    goto_inline.error_msg();
  }
  
  if(goto_inline.get_error_found())
    throw 0;

  // clean up
  for(goto_functionst::function_mapt::iterator
      it=goto_functions.function_map.begin();
      it!=goto_functions.function_map.end();
      it++)
    if(it->first!=goto_functionst::entry_point())
      it->second.body.clear();
}

/*******************************************************************\

Function: goto_inline

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_inline(
  goto_modelt &goto_model,
  message_handlert &message_handler)
{
  const namespacet ns(goto_model.symbol_table);
  goto_inline(goto_model.goto_functions, ns, message_handler);
}

/*******************************************************************\

Function: goto_partial_inline

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_partial_inline(
  goto_functionst &goto_functions,
  const namespacet &ns,
  message_handlert &message_handler,
  unsigned _smallfunc_limit)
{
  goto_inlinet goto_inline(
    goto_functions,
    ns,
    message_handler);
  
  goto_inline.smallfunc_limit=_smallfunc_limit;

  try
  {
    for(goto_functionst::function_mapt::iterator
        it=goto_functions.function_map.begin();
        it!=goto_functions.function_map.end();
        it++)
      if(it->second.body_available())
        goto_inline.goto_inline_rec(it, false);
  }

  catch(int)
  {
    goto_inline.error_msg();
  }

  catch(const char *e)
  {
    goto_inline.str << e;
    goto_inline.error_msg();
  }

  catch(const std::string &e)
  {
    goto_inline.str << e;
    goto_inline.error_msg();
  }

  if(goto_inline.get_error_found())
    throw 0;
}

/*******************************************************************\

Function: goto_partial_inline

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_partial_inline(
  goto_modelt &goto_model,
  message_handlert &message_handler,
  unsigned _smallfunc_limit)
{
  const namespacet ns(goto_model.symbol_table);
  goto_partial_inline(goto_model.goto_functions, ns, message_handler, _smallfunc_limit);
}
