#define EFL_BETA_API_SUPPORT
#define EFL_EO_API_SUPPORT

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <ctype.h>
#include <syslog.h>

#include <e.h>
#include <Eina.h>
#include <Ecore.h>
#include <Ecore_Con.h>
#include <Eeze.h>

#include "e_mod_main.h"

typedef enum
{
   SYNC_OK,     /* V */
   SYNC_NEEDED, /* ! */
   SYNC_NO_DIR, /* X */
   SYNC_FAILED  /* ? */
} Sync_Status;

typedef struct
{
   E_Gadcon_Client *gcc;
   Evas_Object *o_icon;
   E_Menu *menu;

   Eina_List *repos; /* List of Repo */
   Ecore_Exe *daemon_exe;
   Ecore_Exe *sync_exe;
} Instance;

typedef struct
{
   Eina_Stringshare *name;
   Eina_Stringshare *master_name;
   Eina_Bool master_dir_ok;
   Eina_List *machs; /* List of Repo_Mach */

   Instance *inst;
} Repo;

typedef struct
{
   Eina_Stringshare *name;
   Sync_Status status;

   Repo *repo;
} Repo_Mach;

#define PRINT _printf

static E_Module *_module = NULL;

static Eina_Bool
_mkdir(const char *dir)
{
   if (!ecore_file_exists(dir))
     {
        Eina_Bool success = ecore_file_mkdir(dir);
        if (!success) return EINA_FALSE;
     }
   return EINA_TRUE;
}

static int
_printf(const char *fmt, ...)
{
   static FILE *fp = NULL;
   static char *printf_buf = NULL;
   va_list args;
   int printed;

   if (!printf_buf) printf_buf = malloc(50000);
   if (!fp)
     {
        char path[1024];
        sprintf(path, "%s/zync/log", efreet_config_home_get());
        fp = fopen(path, "a");
     }

   va_start(args, fmt);
   printed = vsprintf(printf_buf, fmt, args);
   va_end(args);

   fwrite(printf_buf, 1, printed, fp);
   fflush(fp);

   return printed;
}

static Eina_Bool
_cmd_end_cb(void *data, int type EINA_UNUSED, void *event)
{
   Ecore_Exe_Event_Del *event_info = (Ecore_Exe_Event_Del *)event;
   Ecore_Exe *exe = event_info->exe;
   Instance *inst = ecore_exe_data_get(exe);
   if (!inst || inst != data) return ECORE_CALLBACK_PASS_ON;
   PRINT("EXE END %p - Code %d\n", exe, event_info->exit_code);
   return ECORE_CALLBACK_DONE;
}

static Eina_Bool
_cmd_output_cb(void *data, int type EINA_UNUSED, void *event)
{
   Ecore_Exe_Event_Data *event_data = (Ecore_Exe_Event_Data *)event;
   const char *str = event_data->data;
   Ecore_Exe *exe = event_data->exe;
   Instance *inst = ecore_exe_data_get(exe);
   if (!inst || inst != data) return ECORE_CALLBACK_PASS_ON;

   PRINT("%*s", event_data->size, str);
   if (inst->sync_exe == exe) return ECORE_CALLBACK_DONE;
   while (str && *str != '\0')
     {
        Eina_List *itr;
        Repo *r;
        char *end, *tmp;
        Eina_Bool master_done = EINA_FALSE;
        str = strchr(str, '{');
        if (!str) goto end;
        str++;
        while (*str == ' ') str++;

        /* Find } and check no more { before */
        end = strchr(str, '}');
        if (!end) goto end;
        end--;
        while (*end == ' ') end--;
        tmp = strchr(str, '{');
        if (tmp && tmp <= end) goto end;

        /* Find repo name */
        tmp = strchr(str, ':');
        if (!tmp || str == tmp) goto end;
        EINA_LIST_FOREACH(inst->repos, itr, r)
           if (!strncmp(r->name, str, tmp - str)) goto repo_found;
        r = calloc(1, sizeof(*r));
        r->name = eina_stringshare_add_length(str, tmp - str);
        r->inst = inst;
        inst->repos = eina_list_append(inst->repos, r);

repo_found:
        str = tmp + 1;
        while (str < end)
          {
             Repo_Mach *m;
             char status;

             while (*str == ' ') str++;
             tmp = strchr(str, '(');
             if (!tmp || str == tmp) goto end;
             status = *(tmp + 1);
             if (*(tmp + 2) != ')') goto end;

             if (!master_done)
               {
                  if (!r->master_name || strncmp(r->master_name, str, tmp - str))
                    {
                       eina_stringshare_del(r->master_name);
                       r->master_name = eina_stringshare_add_length(str, tmp - str);
                    }
                  r->master_dir_ok = (status == '*');
                  master_done = EINA_TRUE;
               }
             else
               {
                  EINA_LIST_FOREACH(r->machs, itr, m)
                     if (!strncmp(m->name, str, tmp - str)) goto mach_found;

                  m = calloc(1, sizeof(*m));
                  m->name = eina_stringshare_add_length(str, tmp - str);
                  m->repo = r;
                  r->machs = eina_list_append(r->machs, m);

mach_found:
                  switch (status)
                    {
                     case 'V': m->status = SYNC_OK; break;
                     case '!': m->status = SYNC_NEEDED; break;
                     case 'X': m->status = SYNC_NO_DIR; break;
                     case '?': m->status = SYNC_FAILED; break;
                     default: goto end;
                    }
                  PRINT("Repo %s Mach %s status %d\n", r->name, m->name, m->status);
               }
             str = tmp + 2 + 1;
          }
     }

end:
   return ECORE_CALLBACK_DONE;
}

static Eo *
_icon_create(Eo *parent, const char *path, Eo **wref)
{
   Eo *ic = wref ? *wref : NULL;
   if (!ic)
     {
        ic = elm_icon_add(parent);
        elm_icon_standard_set(ic, path);
        evas_object_show(ic);
        if (wref) efl_wref_add(ic, wref);
     }
   return ic;
}

#if 0
static Eo *
_label_create(Eo *parent, const char *text, Eo **wref)
{
   Eo *label = wref ? *wref : NULL;
   if (!label)
     {
        label = elm_label_add(parent);
        evas_object_size_hint_align_set(label, EVAS_HINT_FILL, EVAS_HINT_FILL);
        evas_object_size_hint_weight_set(label, 0.0, 0.0);
        evas_object_show(label);
        if (wref) efl_wref_add(label, wref);
     }
   elm_object_text_set(label, text);
   return label;
}

static Eo *
_button_create(Eo *parent, const char *text, Eo *icon, Eo **wref, Evas_Smart_Cb cb_func, void *cb_data)
{
   Eo *bt = wref ? *wref : NULL;
   if (!bt)
     {
        bt = elm_button_add(parent);
        evas_object_size_hint_align_set(bt, EVAS_HINT_FILL, EVAS_HINT_FILL);
        evas_object_size_hint_weight_set(bt, 0.0, 0.0);
        evas_object_show(bt);
        if (wref) efl_wref_add(bt, wref);
        if (cb_func) evas_object_smart_callback_add(bt, "clicked", cb_func, cb_data);
     }
   elm_object_text_set(bt, text);
   elm_object_part_content_set(bt, "icon", icon);
   return bt;
}
#endif

static Instance *
_instance_create()
{
   return calloc(1, sizeof(Instance));
}

static void
_instance_delete(Instance *inst)
{
   if (inst->o_icon) evas_object_del(inst->o_icon);

   free(inst);
}

static const char *
_status_icon_get(Sync_Status status)
{
   switch (status)
     {
      case SYNC_OK: return "v_green.png";
      case SYNC_NEEDED: return "excl_orange.png";
      case SYNC_NO_DIR: return "x_red.png";
      case SYNC_FAILED: return "question_red.png";
     }
   return NULL;
}

static void
_show_diff_cb(void *data, E_Menu *menu EINA_UNUSED, E_Menu_Item *menu_item EINA_UNUSED)
{
   char cmd[512];
   Repo_Mach *rmach = data;
   Repo *r = rmach->repo;
   Instance *inst = r->inst;
   sprintf(cmd, "zync --check push %s %s", r->name, rmach->name);

   inst->sync_exe = ecore_exe_pipe_run(cmd,
         ECORE_EXE_PIPE_READ | ECORE_EXE_PIPE_ERROR | ECORE_EXE_USE_SH,
         inst);
   efl_wref_add(inst->sync_exe, &(inst->sync_exe));
}

static void
_update_cb(void *data, E_Menu *menu EINA_UNUSED, E_Menu_Item *menu_item EINA_UNUSED)
{
   char cmd[512];
   Repo_Mach *rmach = data;
   Repo *r = rmach->repo;
   Instance *inst = r->inst;
   sprintf(cmd, "zync push %s %s", r->name, rmach->name);
   PRINT("%s\n", cmd);

   inst->sync_exe = ecore_exe_pipe_run(cmd,
         ECORE_EXE_PIPE_READ | ECORE_EXE_PIPE_ERROR | ECORE_EXE_USE_SH,
         inst);
   efl_wref_add(inst->sync_exe, &(inst->sync_exe));
}

static void
_button_cb_mouse_down(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info)
{
   Instance *inst;
   Evas_Event_Mouse_Down *ev;

   inst = data;
   ev = event_info;

   if (ev->button == 1)
     {
        char buf[1024];
        Eina_List *itr, *itr2;
        Repo *r;
        E_Menu *m, *m2, *m3;
        int x, y;

        m = e_menu_new();
        EINA_LIST_FOREACH(inst->repos, itr, r)
          {
             Sync_Status repo_status = r->master_dir_ok ? SYNC_OK : SYNC_NO_DIR;
             Repo_Mach *rmach;
             E_Menu_Item *mi, *mi2, *mi3;
             char lb_text[256];
             sprintf(lb_text, "%s (%s)", r->name, r->master_name);
             mi = e_menu_item_new(m);
             e_menu_item_label_set(mi, lb_text);
             if (r->machs)
               {
                  m2 = e_menu_new();
                  e_menu_item_submenu_set(mi, m2);
               }
             EINA_LIST_FOREACH(r->machs, itr2, rmach)
               {
                  const char *icon_name = _status_icon_get(rmach->status);
                  mi2 = e_menu_item_new(m2);
                  e_menu_item_label_set(mi2, rmach->name);
                  if (icon_name)
                    {
                       snprintf(buf, sizeof(buf), "%s/%s", e_module_dir_get(_module), icon_name);
                       e_menu_item_icon_file_set(mi2, buf);
                    }
                  if (!inst->sync_exe && r->master_dir_ok && rmach->status == SYNC_NEEDED)
                    {
                       m3 = e_menu_new();
                       e_menu_item_submenu_set(mi2, m3);
                       mi3 = e_menu_item_new(m3);
                       e_menu_item_label_set(mi3, "Show diff");
                       e_menu_item_callback_set(mi3, _show_diff_cb, rmach);

                       mi3 = e_menu_item_new(m3);
                       e_menu_item_label_set(mi3, "Update");
                       e_menu_item_callback_set(mi3, _update_cb, rmach);
                    }

                  if (repo_status < rmach->status) repo_status = rmach->status;
               }

             snprintf(buf, sizeof(buf), "%s/%s", e_module_dir_get(_module), _status_icon_get(repo_status));
             e_menu_item_icon_file_set(mi, buf);
          }

        e_gadcon_canvas_zone_geometry_get(inst->gcc->gadcon, &x, &y, NULL, NULL);
        e_menu_activate_mouse(m,
              e_zone_current_get(),
              x + ev->output.x, y + ev->output.y, 1, 1,
              E_MENU_POP_DIRECTION_DOWN, ev->timestamp);
     }
}

static E_Gadcon_Client *
_gc_init(E_Gadcon *gc, const char *name, const char *id, const char *style)
{
   Instance *inst;
   E_Gadcon_Client *gcc;
   char buf[4096];

   sprintf(buf, "%s/zync", efreet_config_home_get());
   _mkdir(buf);

   inst = _instance_create();

   snprintf(buf, sizeof(buf), "%s/icon.png", e_module_dir_get(_module));

   inst->o_icon = _icon_create(gc->evas, buf, NULL);

   gcc = e_gadcon_client_new(gc, name, id, style, inst->o_icon);
   gcc->data = inst;
   inst->gcc = gcc;

   evas_object_event_callback_add(inst->o_icon, EVAS_CALLBACK_MOUSE_DOWN,
				  _button_cb_mouse_down, inst);

   ecore_event_handler_add(ECORE_EXE_EVENT_DATA, _cmd_output_cb, inst);
   ecore_event_handler_add(ECORE_EXE_EVENT_ERROR, _cmd_output_cb, inst);
   ecore_event_handler_add(ECORE_EXE_EVENT_DEL, _cmd_end_cb, inst);

   inst->daemon_exe = ecore_exe_pipe_run("zync --delim daemon",
         ECORE_EXE_PIPE_READ | ECORE_EXE_PIPE_ERROR | ECORE_EXE_USE_SH,
         inst);
   PRINT("EXE %p\n", inst->daemon_exe);
   efl_wref_add(inst->daemon_exe, &(inst->daemon_exe));

   return gcc;
}

static void
_gc_shutdown(E_Gadcon_Client *gcc)
{
   Instance *inst = gcc->data;
   ecore_exe_kill(inst->daemon_exe);
   _instance_delete(inst);
}

static void
_gc_orient(E_Gadcon_Client *gcc, E_Gadcon_Orient orient EINA_UNUSED)
{
   e_gadcon_client_aspect_set(gcc, 32, 16);
   e_gadcon_client_min_size_set(gcc, 32, 16);
}

static const char *
_gc_label(const E_Gadcon_Client_Class *client_class EINA_UNUSED)
{
   return "zync";
}

static Evas_Object *
_gc_icon(const E_Gadcon_Client_Class *client_class EINA_UNUSED, Evas *evas)
{
   char buf[4096];

   if (!_module) return NULL;

   snprintf(buf, sizeof(buf), "%s/icon.png", e_module_dir_get(_module));

   return _icon_create(evas, buf, NULL);
}

static const char *
_gc_id_new(const E_Gadcon_Client_Class *client_class)
{
   char buf[32];
   static int id = 0;
   sprintf(buf, "%s.%d", client_class->name, ++id);
   return eina_stringshare_add(buf);
}

EAPI E_Module_Api e_modapi =
{
   E_MODULE_API_VERSION, "zync"
};

static const E_Gadcon_Client_Class _gc_class =
{
   GADCON_CLIENT_CLASS_VERSION, "zync",
   {
      _gc_init, _gc_shutdown, _gc_orient, _gc_label, _gc_icon, _gc_id_new, NULL, NULL
   },
   E_GADCON_CLIENT_STYLE_PLAIN
};

EAPI void *
e_modapi_init(E_Module *m)
{
   ecore_init();
   ecore_con_init();
   ecore_con_url_init();
   efreet_init();

   _module = m;
   e_gadcon_provider_register(&_gc_class);

   return m;
}

EAPI int
e_modapi_shutdown(E_Module *m EINA_UNUSED)
{
   e_gadcon_provider_unregister(&_gc_class);

   _module = NULL;
   efreet_shutdown();
   ecore_con_url_shutdown();
   ecore_con_shutdown();
   ecore_shutdown();
   return 1;
}

EAPI int
e_modapi_save(E_Module *m EINA_UNUSED)
{
   return 1;
}
