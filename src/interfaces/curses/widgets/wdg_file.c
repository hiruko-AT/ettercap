/*
    WDG -- file widget

    Copyright (C) ALoR

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

    $Id: wdg_file.c,v 1.4 2003/11/26 20:39:16 alor Exp $
*/

#include <wdg.h>

#include <ncurses.h>
#include <menu.h>

#include <stdarg.h>
#include <sys/stat.h>

#include <dirent.h>

#ifndef HAVE_SCANDIR
   #include <missing/scandir.h>
#endif

/* GLOBALS */

struct wdg_file_handle {
   WINDOW *win;
   MENU *m;
   WINDOW *mwin;
   ITEM **items;
   size_t nitems;
   char curpath[PATH_MAX];
   char initpath[PATH_MAX];
   void (*callback)(char *path, char *file);
};

#define FILE_COLS    50
#define FILE_LINES   18

/* PROTOS */

void wdg_create_file(struct wdg_object *wo);

static int wdg_file_destroy(struct wdg_object *wo);
static int wdg_file_resize(struct wdg_object *wo);
static int wdg_file_redraw(struct wdg_object *wo);
static int wdg_file_get_focus(struct wdg_object *wo);
static int wdg_file_lost_focus(struct wdg_object *wo);
static int wdg_file_get_msg(struct wdg_object *wo, int key, struct wdg_mouse_event *mouse);

static void wdg_file_borders(struct wdg_object *wo);
static void wdg_file_menu_destroy(struct wdg_object *wo);
static void wdg_file_menu_create(struct wdg_object *wo);

static int wdg_file_virtualize(int key);
static int wdg_file_driver(struct wdg_object *wo, int key, struct wdg_mouse_event *mouse);
static void wdg_file_callback(struct wdg_object *wo, char *path, char *file);

void wdg_file_add_callback(wdg_t *wo, void (*callback)(char *path, char *file));

/*******************************************/

/* 
 * called to create the file dialog
 */
void wdg_create_file(struct wdg_object *wo)
{
   struct wdg_file_handle *ww;
   
   /* set the callbacks */
   wo->destroy = wdg_file_destroy;
   wo->resize = wdg_file_resize;
   wo->redraw = wdg_file_redraw;
   wo->get_focus = wdg_file_get_focus;
   wo->lost_focus = wdg_file_lost_focus;
   wo->get_msg = wdg_file_get_msg;

   WDG_SAFE_CALLOC(wo->extend, 1, sizeof(struct wdg_file_handle));
   
   /* 
    * remember the initial path.
    * this has to be restored after the file is selected 
    */
   ww = (struct wdg_file_handle *)wo->extend;
   getcwd(ww->initpath, PATH_MAX);
   
   /* default dimentions */
   wo->x1 = (current_screen.cols - FILE_COLS) / 2;
   wo->y1 = (current_screen.lines - FILE_LINES) / 2;
   wo->x2 = -wo->x1;
   wo->y2 = -wo->y1;
}

/* 
 * called to destroy the file dialog
 */
static int wdg_file_destroy(struct wdg_object *wo)
{
   WDG_WO_EXT(struct wdg_file_handle, ww);

   /* erase the window */
   wbkgd(ww->win, COLOR_PAIR(wo->screen_color));
   werase(ww->win);
   wnoutrefresh(ww->win);

   /* destroy the internal menu */
   wdg_file_menu_destroy(wo);

   /* dealloc the structures */
   delwin(ww->win);
   
   /* restore the initial workind direcory */
   chdir(ww->initpath);

   WDG_SAFE_FREE(wo->extend);

   return WDG_ESUCCESS;
}

/* 
 * called to resize the file dialog
 */
static int wdg_file_resize(struct wdg_object *wo)
{
   wdg_file_redraw(wo);

   return WDG_ESUCCESS;
}

/* 
 * called to redraw the file dialog
 */
static int wdg_file_redraw(struct wdg_object *wo)
{
   WDG_WO_EXT(struct wdg_file_handle, ww);
   size_t c = wdg_get_ncols(wo);
   size_t l = wdg_get_nlines(wo);
   size_t x = wdg_get_begin_x(wo);
   size_t y = wdg_get_begin_y(wo);
 
   /* the window already exist */
   if (ww->win) {
      /* erase the border */
      wbkgd(ww->win, COLOR_PAIR(wo->window_color));
      werase(ww->win);
      /* destroy the file list */ 
      wdg_file_menu_destroy(wo);
      
      touchwin(ww->win);
      wnoutrefresh(ww->win);
      
      /* resize the window and draw the new border */
      mvwin(ww->win, y, x);
      wresize(ww->win, l, c);
      wdg_file_borders(wo);
      
      /* create the file list */
      wdg_file_menu_create(wo);
     
      touchwin(ww->win);

   /* the first time we have to allocate the window */
   } else {

      /* create the menu window (fixed dimensions) */
      if ((ww->win = newwin(l, c, y, x)) == NULL)
         return -WDG_EFATAL;

      /* draw the titles */
      wdg_file_borders(wo);
      
      /* create the file list */
      wdg_file_menu_create(wo);

      /* set the window color */
      wbkgd(ww->win, COLOR_PAIR(wo->window_color));
      redrawwin(ww->win);

      /* no scrolling */
      scrollok(ww->win, FALSE);
   }
   
   /* refresh the window */
   touchwin(ww->win);
   wnoutrefresh(ww->win);

   touchwin(ww->mwin);
   wnoutrefresh(ww->mwin);
   
   wo->flags |= WDG_OBJ_VISIBLE;

   return WDG_ESUCCESS;
}

/* 
 * called when the file dialog gets the focus
 */
static int wdg_file_get_focus(struct wdg_object *wo)
{
   /* set the flag */
   wo->flags |= WDG_OBJ_FOCUSED;

   /* redraw the window */
   wdg_file_redraw(wo);
   
   return WDG_ESUCCESS;
}

/* 
 * called when the file dialog looses the focus
 */
static int wdg_file_lost_focus(struct wdg_object *wo)
{
   /* set the flag */
   wo->flags &= ~WDG_OBJ_FOCUSED;
  
   /* redraw the window */
   wdg_file_redraw(wo);
   
   return WDG_ESUCCESS;
}

/* 
 * called by the messages dispatcher when the file dialog is focused
 */
static int wdg_file_get_msg(struct wdg_object *wo, int key, struct wdg_mouse_event *mouse)
{
   WDG_WO_EXT(struct wdg_file_handle, ww);

   /* handle the message */
   switch (key) {
         
      case KEY_MOUSE:
         /* is the mouse event within our edges ? */
         if (wenclose(ww->win, mouse->y, mouse->x)) {
            wdg_set_focus(wo);
            /* pass it to the menu */
            if (wdg_file_driver(wo, key, mouse) != WDG_ESUCCESS)
               wdg_file_redraw(wo);
         } else 
            return -WDG_ENOTHANDLED;
         break;

      case KEY_RETURN:
      case KEY_DOWN:
      case KEY_UP:
      case KEY_PPAGE:
      case KEY_NPAGE:
         /* move only if focused */
         if (wo->flags & WDG_OBJ_FOCUSED) {
            if (wdg_file_driver(wo, key, mouse) != WDG_ESUCCESS)
               wdg_file_redraw(wo);
         } else
            return -WDG_ENOTHANDLED;
         break;
         
      /* message not handled */
      default:
         return -WDG_ENOTHANDLED;
         break;
   }
  
   return WDG_ESUCCESS;
}

/*
 * draw the file dialog borders
 */
static void wdg_file_borders(struct wdg_object *wo)
{
   WDG_WO_EXT(struct wdg_file_handle, ww);
   size_t c = wdg_get_ncols(wo);
      
   /* the object was focused */
   if (wo->flags & WDG_OBJ_FOCUSED) {
      wattron(ww->win, A_BOLD);
      wbkgdset(ww->win, COLOR_PAIR(wo->focus_color));
   } else
      wbkgdset(ww->win, COLOR_PAIR(wo->border_color));

   /* draw the borders */
   box(ww->win, 0, 0);
   
   /* set the title color */
   wbkgdset(ww->win, COLOR_PAIR(wo->title_color));
   
   /* there is a title: print it */
   if (wo->title) {
      switch (wo->align) {
         case WDG_ALIGN_LEFT:
            wmove(ww->win, 0, 3);
            break;
         case WDG_ALIGN_CENTER:
            wmove(ww->win, 0, (c - strlen(wo->title)) / 2);
            break;
         case WDG_ALIGN_RIGHT:
            wmove(ww->win, 0, c - strlen(wo->title) - 3);
            break;
      }
      wprintw(ww->win, wo->title);
   }
   
   /* restore the attribute */
   if (wo->flags & WDG_OBJ_FOCUSED)
      wattroff(ww->win, A_BOLD);
}

/*
 * stransform keys into menu commands 
 */
static int wdg_file_virtualize(int key)
{
   switch (key) {
      case KEY_RETURN:
      case KEY_EXIT:
         return (MAX_COMMAND + 1);
      case KEY_NPAGE:
         return (REQ_SCR_DPAGE);
      case KEY_PPAGE:
         return (REQ_SCR_UPAGE);
      case KEY_DOWN:
         return (REQ_NEXT_ITEM);
      case KEY_UP:
         return (REQ_PREV_ITEM);
      default:
         if (key != KEY_MOUSE)
            beep();
         return (key);
   }
}

/*
 * sends command to the menu 
 */
static int wdg_file_driver(struct wdg_object *wo, int key, struct wdg_mouse_event *mouse)
{
   WDG_WO_EXT(struct wdg_file_handle, ww);
   int c;
   struct stat buf;
   
   c = menu_driver(ww->m, wdg_file_virtualize(key) );
   
   /* skip non selectable items */
   if ( !(item_opts(current_item(ww->m)) & O_SELECTABLE) )
      c = menu_driver(ww->m, wdg_file_virtualize(key) );

   /* one item has been selected */
   if (c == E_UNKNOWN_COMMAND) {
      /* the item is not selectable (probably selected with mouse) */
      if ( !(item_opts(current_item(ww->m)) & O_SELECTABLE) )
         return WDG_ESUCCESS;
         
      stat(item_name(current_item(ww->m)), &buf);
      /* if it is a directory, change to it */
      if (S_ISDIR(buf.st_mode)) {
         chdir(item_name(current_item(ww->m)));
         return -WDG_ENOTHANDLED;
      } else {
         /* invoke the callback and return */
         wdg_file_callback(wo, ww->curpath, (char *)item_name(current_item(ww->m)));
         return WDG_ESUCCESS;
      }
   
   }

   wnoutrefresh(ww->mwin);
      
   return WDG_ESUCCESS;
}

/*
 * delete the internal menu for the files 
 */
static void wdg_file_menu_destroy(struct wdg_object *wo)
{
   WDG_WO_EXT(struct wdg_file_handle, ww);
   int i = 0;
   
   /* nothing to free */
   if (ww->nitems == 0)
      return;
   
   /* free all the items */
   while(ww->items[i] != NULL)
      free_item(ww->items[i++]);
  
   /* free the array */
   WDG_SAFE_FREE(ww->items);

   free_menu(ww->m);
   
   /* reset the counter */
   ww->nitems = 0;
}

/*
 * create the internal menu for the files 
 */
static void wdg_file_menu_create(struct wdg_object *wo)
{
   WDG_WO_EXT(struct wdg_file_handle, ww);
   struct dirent **namelist;
   int n, i;
   int mrows, mcols;
   size_t c = wdg_get_ncols(wo);
   size_t x = wdg_get_begin_x(wo);
   size_t y = wdg_get_begin_y(wo);
   struct stat buf;

   /* the menu is already posted */
   if (ww->nitems)
      return;
  
   /* get the working directory */
   getcwd(ww->curpath, PATH_MAX);
         
   /* scan the directory */
   n = scandir(".", &namelist, 0, alphasort);

   /* on error display the message in the box */
   if (n < 0) {
      ww->nitems++;
      WDG_SAFE_REALLOC(ww->items, ww->nitems * sizeof(ITEM *));
      ww->items[ww->nitems - 1] = new_item("Cannot open the directory", "");
      item_opts_off(ww->items[ww->nitems - 1], O_SELECTABLE);
   }

   /* for each directory in the directory */
   for (i = 0; i < n; i++) {
      
      /* skip the current dir */
      if (!strcmp(namelist[i]->d_name, "."))
         continue;
      
      /* get the file properties */
      stat(namelist[i]->d_name, &buf);
      
      if (S_ISDIR(buf.st_mode)) {
         ww->nitems++;
         WDG_SAFE_REALLOC(ww->items, ww->nitems * sizeof(ITEM *));
         ww->items[ww->nitems - 1] = new_item(namelist[i]->d_name, "[...]");
      }
      // if not readable
      //item_opts_off(ww->items[ww->nitems - 1], O_SELECTABLE);
   }
   
   /* and now add the files */
   for (i = 0; i < n; i++) {
      
      /* get the file properties */
      stat(namelist[i]->d_name, &buf);
      
      if (!S_ISDIR(buf.st_mode)) {
         ww->nitems++;
         WDG_SAFE_REALLOC(ww->items, ww->nitems * sizeof(ITEM *));
         ww->items[ww->nitems - 1] = new_item(namelist[i]->d_name, "");
      }
   }

   /* null terminate the array */
   WDG_SAFE_REALLOC(ww->items, (ww->nitems + 1) * sizeof(ITEM *));
   ww->items[ww->nitems] = NULL;
     
   /* create the menu */
   ww->m = new_menu(ww->items);

   /* set the dimensions */
   set_menu_format(ww->m, FILE_LINES - 1, 1);
   set_menu_spacing(ww->m, 2, 0, 0);

   /* get the geometry to make a window */
   scale_menu(ww->m, &mrows, &mcols);

   /* 
    * if the menu is larger than the main window
    * adapt to the new dimensions
    */
   if (mcols > (int)c - 4) {
      wo->x1 = (current_screen.cols - (mcols + 4)) / 2;
      wo->x2 = -wo->x1;
      wdg_file_redraw(wo);
      return;
   }
   /* create the window for the menu */
   ww->mwin = newwin(mrows, MAX(mcols, (int)c - 4), y + 1, x + 2);
   /* set the color */
   wbkgd(ww->mwin, COLOR_PAIR(wo->window_color));
   keypad(ww->mwin, TRUE);
  
   /* associate with the menu */
   set_menu_win(ww->m, ww->mwin);
   
   /* the subwin for the menu */
   set_menu_sub(ww->m, derwin(ww->mwin, mrows + 1, mcols, 1, 1));

   /* menu attributes */
   set_menu_mark(ww->m, "");
   set_menu_grey(ww->m, COLOR_PAIR(wo->window_color));
   set_menu_back(ww->m, COLOR_PAIR(wo->window_color));
   set_menu_fore(ww->m, COLOR_PAIR(wo->window_color) | A_REVERSE | A_BOLD);
   
   /* display the menu */
   post_menu(ww->m);

   wnoutrefresh(ww->mwin);
   
}

/*
 * destroy the dialog and
 * call the function associated to the file open dialog
 */
static void wdg_file_callback(struct wdg_object *wo, char *path, char *file)
{
   WDG_WO_EXT(struct wdg_file_handle, ww);
   void (*callback)(char *, char *);
   
   callback = ww->callback;
   wdg_destroy_object(&wo);
   wdg_redraw_all();
   WDG_EXECUTE(callback, path, file);
}

/*
 * the user should use it to associate a callback to the file selection
 */
void wdg_file_add_callback(wdg_t *wo, void (*callback)(char *path, char *file))
{
   WDG_WO_EXT(struct wdg_file_handle, ww);
  
   ww->callback = callback;
}

/* EOF */

// vim:ts=3:expandtab
