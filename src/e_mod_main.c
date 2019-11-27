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
   Eina_Stringshare *name;
   Sync_Status status;
} Repo_Mach;

typedef struct
{
   Eina_Stringshare *name;
   Eina_Stringshare *master_name;
   Eina_Bool master_dir_ok;
   Eina_List *machs; /* List of Repo_Mach */
} Repo;

typedef struct
{
   E_Gadcon_Client *gcc;
   Evas_Object *o_icon;

   Ecore_Exe *sync_exe;
   Eina_List *repos; /* List of Repo */
} Instance;

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
   char printf_buf[1024];
   va_list args;
   int printed;

   if (!fp)
     {
        char path[1024];
        sprintf(path, "%s/zync/log", efreet_config_home_get());
        fp = fopen(path, "a");
     }

   va_start(args, fmt);
   printed = vsprintf(printf_buf, fmt, args);
   va_end(args);

   fwrite(printf_buf, 1, strlen(printf_buf), fp);
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

static void
_button_cb_mouse_down(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info)
{
   Instance *inst;
   Evas_Event_Mouse_Down *ev;

   inst = data;
   ev = event_info;

   if (ev->button == 1)
     {
        Eina_List *itr, *itr2;
        Repo *r;
        E_Menu *m, *m2;
        int x, y;

        m = e_menu_new();
        EINA_LIST_FOREACH(inst->repos, itr, r)
          {
             Repo_Mach *rmach;
             E_Menu_Item *mi, *mi2;
             char lb_text[256];
             sprintf(lb_text, "%s (%s)", r->name, r->master_name);
             mi = e_menu_item_new(m);
             e_menu_item_label_set(mi, lb_text);
             m2 = e_menu_new();
             e_menu_item_submenu_set(mi, m2);
             EINA_LIST_FOREACH(r->machs, itr2, rmach)
               {
                  mi2 = e_menu_item_new(m2);
                  e_menu_item_label_set(mi2, rmach->name);
//                  e_menu_item_callback_set(mi2, _image_selected, dev);
               }
          }

        m = e_gadcon_client_util_menu_items_append(inst->gcc, m, 0);
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

   inst->sync_exe = ecore_exe_pipe_run("zync daemon",
         ECORE_EXE_PIPE_READ | ECORE_EXE_PIPE_ERROR | ECORE_EXE_USE_SH,
         inst);
   PRINT("EXE %p\n", inst->sync_exe);
   efl_wref_add(inst->sync_exe, &(inst->sync_exe));

   return gcc;
}

static void
_gc_shutdown(E_Gadcon_Client *gcc)
{
   Instance *inst = gcc->data;
   ecore_exe_kill(inst->sync_exe);
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
