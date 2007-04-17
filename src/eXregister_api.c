/*
  eXosip - This is the eXtended osip library.
  Copyright (C) 2002,2003,2004,2005,2006,2007  Aymeric MOIZARD  - jack@atosc.org
  
  eXosip is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
  
  eXosip is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/


#ifdef ENABLE_MPATROL
#include <mpatrol.h>
#endif


#include "eXosip2.h"

extern eXosip_t eXosip;

static int _eXosip_register_build_register (eXosip_reg_t * jr,
                                            osip_message_t ** _reg);

static eXosip_reg_t *
eXosip_reg_find (int rid)
{
  eXosip_reg_t *jr;

  for (jr = eXosip.j_reg; jr != NULL; jr = jr->next)
    {
      if (jr->r_id == rid)
        {
          return jr;
        }
    }
  return NULL;
}

int eXosip_register_remove (int rid)
{
  eXosip_reg_t *jr;
  jr = eXosip_reg_find (rid);
  if (jr == NULL)
    {
      /* fprintf(stderr, "eXosip: no registration info saved!\n"); */
      return -1;
    }
  jr->r_reg_period = 0;
  REMOVE_ELEMENT (eXosip.j_reg, jr);
  eXosip_reg_free (jr);
  jr=NULL;
  return 0;
}

static int
_eXosip_register_build_register (eXosip_reg_t * jr, osip_message_t ** _reg)
{
  osip_message_t *reg = NULL;
  int i;

  reg = NULL;
  *_reg = NULL;

  if (jr->r_last_tr != NULL)
    {
      if (jr->r_last_tr->state != NICT_TERMINATED
          && jr->r_last_tr->state != NICT_COMPLETED)
        return -1;
      else
        {
          osip_message_t *last_response = NULL;
          osip_transaction_t *tr;

          osip_message_clone (jr->r_last_tr->orig_request, &reg);
          if (reg == NULL)
            return -1;
          /* reg = jr->r_last_tr->orig_request; */
          if (jr->r_last_tr->last_response != NULL)
            {
              osip_message_clone (jr->r_last_tr->last_response, &last_response);
              if (last_response == NULL)
                {
                  osip_message_free (reg);
                  return -1;
                }
            }

          __eXosip_delete_jinfo (jr->r_last_tr);
          tr = jr->r_last_tr;
          jr->r_last_tr = NULL;
          osip_list_add (eXosip.j_transactions, tr, 0);

          /* modify the REGISTER request */
          {
            int osip_cseq_num = osip_atoi (reg->cseq->number);
            int length = strlen (reg->cseq->number);


	    osip_list_special_free(&reg->authorizations, (void *(*)(void *)) &osip_authorization_free);
	    osip_list_special_free(&reg->proxy_authorizations, (void *(*)(void *)) &osip_proxy_authorization_free);


            if (-1 == eXosip_update_top_via (reg))
              {
                osip_message_free (reg);
                if (last_response != NULL)
                  osip_message_free (last_response);
                return -1;
              }

            osip_cseq_num++;
            osip_free (reg->cseq->number);
            reg->cseq->number = (char *) osip_malloc (length + 2);      /* +2 like for 9 to 10 */
            sprintf (reg->cseq->number, "%i", osip_cseq_num);

			{
              osip_header_t *exp;

              osip_message_header_get_byname (reg, "expires", 0, &exp);
              if (exp!=NULL)
			  {
				  if (exp->hvalue!=NULL)
					  osip_free (exp->hvalue);
				  exp->hvalue = (char *) osip_malloc (10);
				  snprintf (exp->hvalue, 9, "%i", jr->r_reg_period);
			  }
            }

            osip_message_force_update (reg);
          }

          if (last_response != NULL)
            {
              if (MSG_IS_STATUS_4XX (last_response))
                {
                  eXosip_add_authentication_information (reg, last_response);
                }
			  else
                  eXosip_add_authentication_information (reg, NULL);
              osip_message_free (last_response);
            }
        }
    }
  if (reg == NULL)
    {
      i = generating_register (&reg, eXosip.transport,
                               jr->r_aor, jr->r_registrar, jr->r_contact,
                               jr->r_reg_period);
      if (i != 0)
        {
          return -2;
        }
    }

  *_reg = reg;
  return 0;
}

int
eXosip_register_build_initial_register (const char *from, const char *proxy,
                                        const char *contact, int expires,
                                        osip_message_t ** reg)
{
  eXosip_reg_t *jr = NULL;
  int i;

  *reg = NULL;

  /* Avoid adding the same registration info twice to prevent mem leaks */
  for (jr = eXosip.j_reg; jr != NULL; jr = jr->next)
    {
      if (strcmp (jr->r_aor, from) == 0 && strcmp (jr->r_registrar, proxy) == 0)
        {
	  REMOVE_ELEMENT (eXosip.j_reg, jr);
	  eXosip_reg_free (jr);
	  jr=NULL;
          break;
        }
    }
  if (jr == NULL)
    {
      /* Add new registration info */
      i = eXosip_reg_init (&jr, from, proxy, contact);
      if (i != 0)
        {
          OSIP_TRACE (osip_trace
                      (__FILE__, __LINE__, OSIP_ERROR, NULL,
                       "eXosip: cannot register! "));
          return i;
        }
      ADD_ELEMENT (eXosip.j_reg, jr);
    }

  /* build register */
  jr->r_reg_period = expires;
  if (jr->r_reg_period <= 0)      /* too low */
    jr->r_reg_period = 0;
  else if (jr->r_reg_period < 100)      /* too low */
    jr->r_reg_period = 100;

  i = _eXosip_register_build_register (jr, reg);
  if (i != 0)
    {
      OSIP_TRACE (osip_trace
                  (__FILE__, __LINE__, OSIP_ERROR, NULL,
                   "eXosip: cannot build REGISTER!\n"));
      *reg = NULL;
      return i;
    }
  return jr->r_id;
}

int
eXosip_register_build_register (int rid, int expires, osip_message_t ** reg)
{
  eXosip_reg_t *jr;
  int i;

  *reg = NULL;
  jr = eXosip_reg_find (rid);
  if (jr == NULL)
    {
      /* fprintf(stderr, "eXosip: no registration info saved!\n"); */
      return -1;
    }
  jr->r_reg_period = expires;
  if (jr->r_reg_period == 0)
    {
    } /* unregistration */
  else if (jr->r_reg_period > 3600)
    jr->r_reg_period = 3600;
  else if (jr->r_reg_period < 100)      /* too low */
    jr->r_reg_period = 100;

  if (jr->r_last_tr != NULL)
    {
      if (jr->r_last_tr->state != NICT_TERMINATED
          && jr->r_last_tr->state != NICT_COMPLETED)
        {
          /* fprintf(stderr, "eXosip: a registration is already pending!\n"); */
          return -1;
        }
    }

  i = _eXosip_register_build_register (jr, reg);
  if (i != 0)
    {
      OSIP_TRACE (osip_trace
                  (__FILE__, __LINE__, OSIP_ERROR, NULL,
                   "eXosip: cannot build REGISTER!"));
      *reg = NULL;
      return i;
    }
  return 0;
}

int
eXosip_register_send_register (int rid, osip_message_t * reg)
{
  osip_transaction_t *transaction;
  osip_event_t *sipevent;
  eXosip_reg_t *jr;
  int i;

  jr = eXosip_reg_find (rid);
  if (jr == NULL)
    {
      osip_message_free (reg);
      return -1;
    }

  if (jr->r_last_tr != NULL)
    {
      if (jr->r_last_tr->state != NICT_TERMINATED
          && jr->r_last_tr->state != NICT_COMPLETED)
        {
          osip_message_free (reg);
          return -1;
        }
    }

  if (reg == NULL)
    {
      i = _eXosip_register_build_register (jr, &reg);
      if (i != 0)
        {
          OSIP_TRACE (osip_trace
                      (__FILE__, __LINE__, OSIP_ERROR, NULL,
                       "eXosip: cannot build REGISTER!"));
          return i;
        }
    }

  i = _eXosip_transaction_init (&transaction, NICT, eXosip.j_osip, reg);
  if (i != 0)
    {
      /* TODO: release the j_call.. */

      osip_message_free (reg);
      return -2;
    }

  jr->r_last_tr = transaction;

  /* send REGISTER */
  sipevent = osip_new_outgoing_sipmessage (reg);
  sipevent->transactionid = transaction->transactionid;
  osip_message_force_update (reg);

  osip_transaction_add_event (transaction, sipevent);
  __eXosip_wakeup ();
  return 0;
}
