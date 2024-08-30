#include <fstream>
#include <iostream>
#include <imgui.h>

#include <module.h>
#include <core.h>
#include <gui/gui.h>
#include <gui/style.h>
#include <utils/freq_formatting.h>
#include <signal_path/signal_path.h>
#include <gui/file_dialogs.h>



/*
REMAINING ISSUES
-- multi-VFO handling
-- bookmark import (test this)

*/


SDRPP_MOD_INFO{
    /* Name:            */ "aaed_freq_manager",
    /* Description:     */ "Aaediwen's Frequency Manager for SDR++",
    /* Author:          */ "Aaediwen",
    /* Version:         */ 0, 1, 0,
    /* Max instances    */ 1
};

class aaed_freq_manager : public ModuleManager::Instance {
     public:
          aaed_freq_manager(std::string name) {
               this->name = name;
               this->aaedcfg=0;
               this->importDialog=0;
               this->exportDialog=0;
               loadConfig();
               this->tempvals.editNameStr=new std::string;
               fftRedrawHandler.ctx = this;
               fftRedrawHandler.handler = fftRedraw;
                  
               inputHandler.ctx = this;
               inputHandler.handler = fftInput;

               gui::menu.registerEntry(name, menuHandler, this, NULL);
               gui::waterfall.onFFTRedraw.bindHandler(&fftRedrawHandler);
               gui::waterfall.onInputProcess.bindHandler(&inputHandler);
          }

          ~aaed_freq_manager() {
               resetConfig();
               gui::menu.removeEntry(name);
               gui::waterfall.onFFTRedraw.unbindHandler(&fftRedrawHandler);
               gui::waterfall.onInputProcess.unbindHandler(&inputHandler);
          }

          void postInit() {}

          void enable() {
               enabled = true;
          }

          void disable() {
               enabled = false;
          }

          bool isEnabled() {
               return enabled;
          }

     private:
     
          EventHandler<ImGui::WaterFall::FFTRedrawArgs> fftRedrawHandler;
          EventHandler<ImGui::WaterFall::InputHandlerArgs> inputHandler;

          struct color {
               int r;
               int g;
               int b;
               int a;
               ImU32 labelColor;
               ImVec4 vector;
          };

          enum display_mode{
              DISP_MODE_OFF,
              DISP_MODE_TOP,
              DISP_MODE_BOTTOM,
              DISP_MODE_COUNT
          };

          enum freq_mode{
              MODE_NFM,
              MODE_WFM,
              MODE_AM,
              MODE_DSB,
              MODE_USB,
              MODE_LSB,
              MODE_CW,
              MODE_RAW
          };

          enum {
              RADIO_IFACE_CMD_GET_MODE,
              RADIO_IFACE_CMD_SET_MODE,
              RADIO_IFACE_CMD_GET_BANDWIDTH,
              RADIO_IFACE_CMD_SET_BANDWIDTH,
              RADIO_IFACE_CMD_GET_SQUELCH_ENABLED,
              RADIO_IFACE_CMD_SET_SQUELCH_ENABLED,
              RADIO_IFACE_CMD_GET_SQUELCH_LEVEL,
              RADIO_IFACE_CMD_SET_SQUELCH_LEVEL,
          };

          const std::string ModeList[8] = {
              "NFM",
              "WFM",
              "AM",
              "DSB",
              "USB",
              "CW",
              "LSB",
              "RAW"
          };

          struct frequency {
              struct frequency *prev;
              std::string name;
              double frequency;
              double bandwidth;
              freq_mode mode;
              bool selected;
              struct frequency *next;
              
          };

          struct category {
              struct category* prev;
              std::string name;
              bool shown;
              struct color color;
              struct frequency *frequencies;
              struct category* next;

          };

          struct  {
               std::string *editNameStr;
               struct color color;
               float rgbf[3];
               struct frequency *bookmark;
               struct frequency temp_freq;
          } tempvals;

          struct config {
              display_mode displayMode;
              struct category *lists;
              struct category *selected;
          };
          
          pfd::open_file* importDialog;
          pfd::save_file* exportDialog;
          struct config *aaedcfg;
          std::string name;
          bool enabled = true;
          
          void resetConfig() {
               if (aaedcfg) {
                    // clear the config
                    struct category *currentList;
                    struct frequency *currentBookmark;
                    struct category* catnext;
                    currentList = aaedcfg->lists;
                    while (currentList) {
                         currentBookmark = currentList->frequencies;
                         if (currentBookmark) {
                              while (currentBookmark) {
                                   struct frequency* tempnext;
                                   tempnext=currentBookmark->next;
                                   delete currentBookmark;
                                   currentBookmark = tempnext;
                               }
                          }
                          catnext = currentList->next;
                          delete currentList;
                          currentList = catnext;
                    }
                    aaedcfg->selected=0;
                    aaedcfg->lists=0;
                    aaedcfg->displayMode=DISP_MODE_OFF;
               }
               return;
          }

          static void saveConfig(struct config *aaedcfg, std::string *path) {
               frequency *current_bookmark;
               category *current_list;
               current_list=aaedcfg->lists;
               json data = json({});
               data["displayMode"]=DISP_MODE_TOP;
               data["selectedList"]=aaedcfg->selected->name.c_str();
               while (current_list) {
                    data["lists"][current_list->name.c_str()]["showOnWaterfall"]=true;
                    data["lists"][current_list->name.c_str()]["color"]["red"] = current_list->color.r;
                    data["lists"][current_list->name.c_str()]["color"]["green"] = current_list->color.g;
                    data["lists"][current_list->name.c_str()]["color"]["blue"] = current_list->color.b;
                    current_bookmark=current_list->frequencies;
                    if (current_bookmark) {
                         while (current_bookmark) {
                              data["lists"][current_list->name.c_str()]["bookmarks"][current_bookmark->name.c_str()]["bandwidth"]=current_bookmark->bandwidth;
                              data["lists"][current_list->name.c_str()]["bookmarks"][current_bookmark->name.c_str()]["frequency"]=current_bookmark->frequency;
                              data["lists"][current_list->name.c_str()]["bookmarks"][current_bookmark->name.c_str()]["mode"]=(int)(current_bookmark->mode);
                              current_bookmark=current_bookmark->next;
                         }
                    }
                    current_list=current_list->next;
               }
               std::string pathstr;
               if ((path==NULL) || ((path->length() < 5))) {
//                    std::cout <<"using normal path for save\n";
                    pathstr=(core::args["root"].s() + "/aaed_freq_manager_config.json");
               } else {
//                    std::cout <<"using "<< path->c_str() << " for save path\n";
                    pathstr=*path;
               }
               std::ofstream f(pathstr);
               f << data.dump(5);
               f.close();
               return;
          }

          void loadConfig() {
      //  	init the basic config struct
                struct category *current;
                bool goodread=true;
                json data;
                if (aaedcfg) {
                     resetConfig();
                } else {
                     aaedcfg=new (struct config);
                }
				aaedcfg->selected=0;
                aaedcfg->lists=0;
				aaedcfg->displayMode=DISP_MODE_TOP;
                tempvals.bookmark=0;
                // read the source JSON
                std::ifstream f((core::args["root"].s() + "/aaed_freq_manager_config.json"));
                if (f.good()) {
                     try {
                     data = json::parse(f);
                     } catch (const json::parse_error &e) {
                       goodread=false;
                     
                     }
                 } else {
                      goodread=false;
                 }
                 if (goodread) {
                     
                

                     // load display mode
                     if (data.contains("DisplayMode")) {
                          aaedcfg->displayMode=data["displayMode"];
                     }
        
                     
                     struct category *last_list;
                     current = 0;
                     last_list=0;
             
                     int r, g, b;
                     // step through the lists
                     for (auto [listName, list] : data["lists"].items()) {
                 // 	     check if the list has either valid or empty bookmarks
                          bool add_list_flag = true;
                          int entrycount=0;
                          if (
                               (list.contains("bookmarks")) && 
                               (!(list["bookmarks"].is_boolean())) 
                             )  {
                               add_list_flag=false;
                               for (auto [bookmarkName, freq] : list["bookmarks"].items()) {
                                    entrycount++;
                                    if (freq.contains("bandwidth") && freq.contains("frequency") && freq.contains("mode")) {
                                         add_list_flag=true;
                                    }
                               }
                               if (!entrycount) {
                                    add_list_flag=true;
                               }
                          } else {
                               add_list_flag=false;
                          }
                 
                 
                          // check if it's a valid list to add
                          if (
                               add_list_flag && 
                               (list["showOnWaterfall"].is_boolean()) 
                             )  {
                               // link in new list
                               last_list=current;
                               current = new struct category;
                               current->next = 0;
                               current->prev=last_list;
                    
                               if (last_list) {
                                    last_list->next=current;
                               } else {
                                    aaedcfg->lists=current;
                               }
                    
                    
                               // add new list info 
                               current->frequencies=0;
                               current->name = listName;
                               current->shown = list["showOnWaterfall"];
                               if (!(listName.compare(data["selectedList"]))) {
                                    aaedcfg->selected=current;
                                    tempvals.bookmark=current->frequencies;
                               }
                               r=255;
                               g=255;
                               b=0;
                               if (list.contains("color")) {
                                    r=list["color"]["red"];
                                    g=list["color"]["green"];
                                    b=list["color"]["blue"];
                               }
                               current->color.r=r;
                               current->color.g=g;
                               current->color.b=b;
                               current->color.a=255;
                               current->color.labelColor=IM_COL32(current->color.r, current->color.g, current->color.b, current->color.a);
                               current->color.vector=ImVec4((float)current->color.r/255, (float)current->color.g/255, (float)current->color.b/255, (float)current->color.a/255);
                               // add frequencies
                               struct frequency *currentBookmark;
                               currentBookmark = current->frequencies;
                               for (auto [bookmarkName, freq] : list["bookmarks"].items()) {
                                    if (freq.contains("bandwidth") && freq.contains("frequency") && freq.contains("mode")) {
                                         if (!currentBookmark) {
                                              currentBookmark=new struct frequency;
                                              current->frequencies = currentBookmark;
                                              currentBookmark->prev = 0;
                                         } else {
                                              currentBookmark->next = new struct frequency;
                                              currentBookmark->next->prev=currentBookmark;
                                              currentBookmark=currentBookmark->next;
                                         }
                                         currentBookmark->name=bookmarkName;
                                         currentBookmark->frequency=freq["frequency"];
                                         currentBookmark->bandwidth=freq["bandwidth"];
                                         currentBookmark->mode=(freq_mode)freq["mode"];
                                         currentBookmark->next = 0;
                                    } 
                               }
                   
                          } 
                     }
                     f.close();
                }                     
                if (!aaedcfg->lists) {
                     // build default config
                     aaedcfg->displayMode=DISP_MODE_TOP;
                     aaedcfg->lists = new struct category;
                     current = aaedcfg->lists;
                     current->prev=0;
                     current->next=0;
                     current->name = "General";
                     current->shown = true;
                     current->frequencies = 0;
                     current->color.r=255;
                     current->color.g=255;
                     current->color.b=0;
                     current->color.a=255;
                     current->color.labelColor=IM_COL32(current->color.r, current->color.g, current->color.b, current->color.a);
                     current->color.vector=ImVec4((float)current->color.r/255, (float)current->color.g/255, (float)current->color.b/255, (float)current->color.a/255);
                     aaedcfg->selected=current;
                     saveConfig(aaedcfg, NULL);
                     
                }
                return;    
          }
          
          static void draw_frequency (ImGui::WaterFall::FFTRedrawArgs args,struct frequency *bookmark, ImU32 labelColor) {
               double centerXpos = args.min.x + std::round((bookmark->frequency - args.lowFreq) * args.freqToPixelRatio);

               // calculate the label box
               ImVec2 nameSize = ImGui::CalcTextSize(bookmark->name.c_str());
               ImVec2 rectMin = ImVec2(centerXpos - (nameSize.x / 2) - 5, args.min.y);
               ImVec2 rectMax = ImVec2(centerXpos + (nameSize.x / 2) + 5, args.min.y + nameSize.y);
               ImVec2 borderRectMin = ImVec2(std::clamp<double>(rectMin.x-1, args.min.x, args.max.x), rectMin.y-1);
               ImVec2 borderRectMax = ImVec2(std::clamp<double>(rectMax.x+1, args.min.x, args.max.x), rectMax.y+1);
               ImVec2 clampedRectMin = ImVec2(std::clamp<double>(rectMin.x, args.min.x, args.max.x), rectMin.y);
               ImVec2 clampedRectMax = ImVec2(std::clamp<double>(rectMax.x, args.min.x, args.max.x), rectMax.y);
                
               // draw the label border
               if (borderRectMax.x - borderRectMin.x > 0) {
                    args.window->DrawList->AddRectFilled(borderRectMin, borderRectMax, IM_COL32(255, 255, 255, 255));
               }
               // draw the pointer stem
               if (bookmark->frequency >= args.lowFreq && bookmark->frequency <= args.highFreq) {
                    args.window->DrawList->AddLine(ImVec2(centerXpos, args.min.y), ImVec2(centerXpos, args.max.y), labelColor);
               }
               // draw the label box
               if (clampedRectMax.x - clampedRectMin.x > 0) {
                    args.window->DrawList->AddRectFilled(clampedRectMin, clampedRectMax, labelColor);
               }
               // draw the text
               if (rectMin.x >= args.min.x && rectMax.x <= args.max.x) {
                    args.window->DrawList->AddText(ImVec2(centerXpos - (nameSize.x / 2), args.min.y), IM_COL32(0, 0, 0, 255), bookmark->name.c_str());
               }
               return;
          }
         
         
          static void fftInput(ImGui::WaterFall::InputHandlerArgs args, void* ctx) {
               aaed_freq_manager* _this = (aaed_freq_manager*)ctx;

               return;
          }
          
          static void fftRedraw(ImGui::WaterFall::FFTRedrawArgs args, void* ctx) {
               aaed_freq_manager* _this = (aaed_freq_manager*)ctx;
               struct category *current_list;
               struct frequency *current_bookmark;
               current_list = _this->aaedcfg->lists;
               while (current_list) {
                    if (current_list->shown && current_list->frequencies) {
                         current_bookmark=current_list->frequencies;
                         while (current_bookmark) {
                              draw_frequency(args, current_bookmark, current_list->color.labelColor);
                              current_bookmark=current_bookmark->next;
                         }
                    }
                    current_list=current_list->next;
               }
               return;
          }
   
          static void importBookmarks(struct config *aaedcfg, std::string path) {
               // read file
               std::ifstream f(path);
               json data = json::parse(f);
               f.close();
               // check if it has lists to import
               if (data.contains("lists")) {
                    for (auto [listName, list] : data["lists"].items()) {
                         if (           (list.contains("bookmarks")) &&     (!(list["bookmarks"].is_boolean()))                 )  {
                              struct category *current, *last, *active_list;
                              current = aaedcfg->lists;
                              bool found = false;
                              struct frequency *current_bookmark;
                              int r, g, b;
                              // valid list to check for bookmarks
                              // see if the list already exists in our config
                              while (current && (!found)) {
                                   if (current->name == listName) {
                                        found=true;
                                   } else {
                                        last=current;
                                        current=current->next;
                                   } // name matches
                              } // while !found
                              if (!current) { // adding a new list entry
                                   active_list = new struct category;
                                   active_list->prev=last;
                                   last->next=active_list;
                                   active_list->next=0;
                                   active_list->color.r = 255;
                                   active_list->color.g = 255;
                                   active_list->color.b = 0;
                                   if (list.contains("color")) {
                                        active_list->color.r = list["color"]["red"];
                                        active_list->color.g = list["color"]["green"];
                                        active_list->color.b = list["color"]["blue"];
                                   }              
                                   active_list->color.a=255;
                                   active_list->color.labelColor=IM_COL32(active_list->color.r, active_list->color.g, active_list->color.b, active_list->color.a);
                                   active_list->color.vector=ImVec4((float)active_list->color.r/255, (float)active_list->color.g/255, (float)active_list->color.b/255, (float)active_list->color.a/255);
                                   active_list->name=listName;
                                   active_list->shown=true;
                                   active_list->frequencies = 0;
                              } else { // re-using an existing list entry
                                   active_list=current;
                              }
                              struct frequency *list_top, *list_bottom;
                              
                              // mark the top and bottom of the list
                              list_top=active_list->frequencies;
                              list_bottom=0;
                              current_bookmark=list_top;
                              if (list_top) {
                                 while (current_bookmark) {
                                    list_bottom=current_bookmark;
                                    current_bookmark=current_bookmark->next;
                                 }
                              }
                              
                              
                              // now itterate through the bookmarks in the json and add any that aren't duplicates
                              for (auto [bookmarkName, freq] : list["bookmarks"].items()) {
                                   if (freq.contains("bandwidth") && freq.contains("frequency") && freq.contains("mode")) {
                                        // check if the bookmark already exists
                                        current_bookmark=list_top;
                                        found=false;
                                        if (current_bookmark) {
                                             while ((!found) && current_bookmark) {
                                                  if ((current_bookmark->frequency == freq["frequency"]) && (freq["mode"]==(int)current_bookmark->mode)) {
                                                       found=true;
                                                  } else {
                                                       current_bookmark=current_bookmark->next;
                                                  } // name matches
                                             }
                                        }
                                        
                                        
                                        
                                        
                                        if (!found) {
                                             struct frequency *new_bookmark;
                                             if (!list_bottom) {
                                                  active_list->frequencies = new struct frequency;
                                                  active_list->frequencies->prev=0;
                                                  new_bookmark = active_list->frequencies;
                                             } else {
                                                  list_bottom->next=new struct frequency;
                                                  list_bottom->next->prev=list_bottom;        
                                                  new_bookmark = list_bottom->next;                                          
                                             }
                                             new_bookmark->next=0;
                                             list_bottom=new_bookmark;
                                             new_bookmark->name=bookmarkName;
                                             new_bookmark->frequency=freq["frequency"];
                                             new_bookmark->bandwidth=freq["bandwidth"];
                                             new_bookmark->mode=(freq_mode)freq["mode"];
                                             std::cout << "Adding Bookmark "<< bookmarkName << " for " << listName<<"\n";
                                        } else {
                                             std::cout << "Not adding duplicate Bookmark "<< bookmarkName << "\n";
                                        }
                                        
                                   } else {
                                        std::cout << "NOT adding bookmark " << bookmarkName << " for " << listName << "\n";
                                   }
                              }
                         } // if has bookmarks 
                    } //foreach list
               } // if contains lists
               return;
          } // import bookmarks function
   
          static struct frequency *delete_bookmark(struct frequency *current_bookmark) {
               struct frequency *result;
               if (current_bookmark->prev) {
                    current_bookmark->prev->next = current_bookmark->next;
               }
               if (current_bookmark->next) {
                    current_bookmark->next->prev = current_bookmark->prev;
               }
               if (current_bookmark->next) {   // deleting a list other than the last one
                    result = current_bookmark->next;
                    delete current_bookmark;
                    return result;
               } else {                   // deleting the last entry
                    if (current_bookmark->prev) {
                         result = current_bookmark->prev;
                         return result;
                    } else {              // deleting the ONLY entry
                         delete current_bookmark;
                         return 0;
                    }
               }
          }   
   
          static struct category *delete_list(struct category *current_list) {
               struct frequency *currentBookmark;
               struct category *result;
               currentBookmark = current_list->frequencies;
               // free all the bookmarks
               if (currentBookmark) {
                    while (currentBookmark) {
                         struct frequency* tempnext;
                         tempnext=currentBookmark->next;
                         delete currentBookmark;
                         currentBookmark = tempnext;
                    }
               }
               // adjust pointers
               if (current_list->prev) {
                    current_list->prev->next = current_list->next;
               }
               if (current_list->next) {
                    current_list->next->prev = current_list->prev;
               }
               // return next selected list
               if (current_list->next) {   // deleting a list other than the last one
                    result = current_list->next;
                    delete current_list;
                    return result;
               } else {                   // deleting the last entry
                    if (current_list->prev) {
                         result = current_list->prev;
                         delete current_list;
                         return result;
                    } else {              // deleting the ONLY entry
                         current_list->prev=0;
                         current_list->next=0;
                         current_list->frequencies=0;
//                         current_list->frequencies=new struct frequency;
//                         current_list->frequencies->prev=0;
//                         current_list->frequencies->next=0;
//                         current_list->frequencies->name=" ";
                         current_list->name="General";
                         return current_list;
                    }
               }
          }
   
          static void menuHandler(void* ctx) {
               struct category *current_list;
               struct frequency *current_bookmark;
               char editNameBuf[1024];
               double editFreqBuf;
               double editBwBuf;
               int editModeBuf;
               float editColBuf[3];

               aaed_freq_manager* _this = (aaed_freq_manager*)ctx;

               // LIST SELECT COMBO BOX
               current_list= _this->aaedcfg->lists;
               if (ImGui::BeginCombo("##aaed_manager_list_sel", _this->aaedcfg->selected->name.c_str())) {
                    while (current_list) {
                         if (!current_list->name.empty()) {
                              bool is_selected = (current_list == _this->aaedcfg->selected);
                              if (ImGui::Selectable(current_list->name.c_str(), is_selected)) {
                                   _this->aaedcfg->selected = current_list;
                                   _this->tempvals.bookmark=current_list->frequencies;
                              }
                         }
                         current_list=current_list->next;
                    }
                    ImGui::EndCombo();
               }

               // SELECTED LIST COLOR SWATCH
               ImVec2 size;
               size = ImVec2( 20.0f, 20.0f );
               ImGui::SameLine();
               ImGui::ColorButton("##aaed_manager_list_color", (_this->aaedcfg->selected->color.vector), 0, size);
               
               // RELOAD CONFIG BUTTON
               ImGui::SameLine();
               if (ImGui::Button(("Reload##_aaed_mgr_reload_cfg_" + _this->name).c_str())) {
                    _this->loadConfig();
               }

               // ADD LIST BUTTON
               ImGui::Separator();
               if (ImGui::Button(("New##_aaed_mgr_new_lst_" + _this->name).c_str())) {
                    // search for a valid NEW LIST name
                    int new_index=0;
                    std::string temp;
                    temp="New List";
                    char scratchpad[15] = "New List";
                    bool found;
                    found=true;
                    while (found) {
                         // set temp to the name we are looking for
                         if (new_index) {
                              sprintf (scratchpad, "New List (%i)", new_index);
                              temp = scratchpad;
                         }
                         // search for a match
                         found=false;
                         current_list = _this->aaedcfg->lists;
                         while (current_list && (!found)) {
                              // match found
                              if (!(temp.compare(current_list->name))) {
                                   new_index++;
                                   found = true;
                              }
                              current_list = current_list->next;
                         }
                    }
                    struct category *new_list;
                    new_list = new struct category;
                    new_list->name = temp;
                    new_list->shown= true;
                    new_list->color.r=255;
                    new_list->color.g=255;
                    new_list->color.b=0;
                    new_list->color.a=255;
                    new_list->color.labelColor=IM_COL32(new_list->color.r, new_list->color.g, new_list->color.b, new_list->color.a);
                    new_list->color.vector=ImVec4((float)new_list->color.r/255, (float)new_list->color.g/255, (float)new_list->color.b/255, (float)new_list->color.a/255);
                    new_list->frequencies=0;
                    _this->tempvals.bookmark=0;
                    new_list->prev = _this->aaedcfg->selected;
                    new_list->next = _this->aaedcfg->selected->next;
                    _this->aaedcfg->selected->next = new_list;
                    _this->aaedcfg->selected=new_list;
                    *(_this->tempvals.editNameStr) =  _this->aaedcfg->selected->name;
                    _this->tempvals.rgbf[0]=(float)(_this->aaedcfg->selected->color.r)/255;
                    _this->tempvals.rgbf[1]=(float)(_this->aaedcfg->selected->color.g)/255;
                    _this->tempvals.rgbf[2]=(float)(_this->aaedcfg->selected->color.b)/255;
                    ImGui::OpenPopup("list_edit_conf");
               }

               // EDIT LIST BUTTON
               ImGui::SameLine();
               if (ImGui::Button(("Edit##_aaed_mgr_edit_lst_" + _this->name).c_str())) {
                    strcpy(editNameBuf, _this->aaedcfg->selected->name.c_str());
                    *(_this->tempvals.editNameStr) = _this->aaedcfg->selected->name;
                    _this->tempvals.rgbf[0]=(float)(_this->aaedcfg->selected->color.r)/255;
                    _this->tempvals.rgbf[1]=(float)(_this->aaedcfg->selected->color.g)/255;
                    _this->tempvals.rgbf[2]=(float)(_this->aaedcfg->selected->color.b)/255;
                    ImGui::OpenPopup("list_edit_conf");
               }
         
               // List edit dialog
               if (ImGui::BeginPopup("list_edit_conf")) {
                    strcpy (editNameBuf, _this->tempvals.editNameStr->c_str());
                    if (ImGui::InputText(("Name##aaed_mgr_edit_name" + _this->name).c_str(), editNameBuf, 1024, 0, NULL)) {
                         *(_this->tempvals.editNameStr) = editNameBuf;
                    }

                    char *labelbuf;
                    labelbuf = (char*)malloc(1024);
                    sprintf(labelbuf, "Color##list_color_%s", _this->aaedcfg->selected->name.c_str());
                    editColBuf[0]=_this->tempvals.rgbf[0];
                    editColBuf[1]=_this->tempvals.rgbf[1];
                    editColBuf[2]=_this->tempvals.rgbf[2];

                    if (ImGui::ColorEdit3((const char*)labelbuf, editColBuf, (ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel))) {
                         _this->tempvals.rgbf[0]=editColBuf[0];
                         _this->tempvals.rgbf[1]=editColBuf[1];
                         _this->tempvals.rgbf[2]=editColBuf[2];
                    }
                    free (labelbuf);
                    if (ImGui::Button("Confirm")) {
                         // apply the changes
                         _this->aaedcfg->selected->name = *(_this->tempvals.editNameStr);
                         _this->aaedcfg->selected->color.r = (int)(_this->tempvals.rgbf[0]*255);
                         _this->aaedcfg->selected->color.g = (int)(_this->tempvals.rgbf[1]*255);
                         _this->aaedcfg->selected->color.b = (int)(_this->tempvals.rgbf[2]*255);
                         _this->aaedcfg->selected->color.a = 255;
                         _this->aaedcfg->selected->color.labelColor=IM_COL32(_this->aaedcfg->selected->color.r, _this->aaedcfg->selected->color.g, _this->aaedcfg->selected->color.b, _this->aaedcfg->selected->color.a);
                         _this->aaedcfg->selected->color.vector=ImVec4(_this->tempvals.rgbf[0], _this->tempvals.rgbf[1], _this->tempvals.rgbf[2], 1);
                         saveConfig(_this->aaedcfg, NULL);
                         ImGui::CloseCurrentPopup();
                    } 
                    if (ImGui::Button("Cancel")) {
                         ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
               }
         
               // DELETE LIST BUTTON
               ImGui::SameLine();
               bool deleteopen;
               size = ImVec2( 40.0f, 20.0f );
               if (ImGui::Button(("Delete##_aaed_mgr_del_lst_" + _this->name).c_str())) {
                    ImGui::OpenPopup("list_delete_conf");
               }
               
               if (ImGui::BeginPopup("list_delete_conf")) {
                    ImGui::Text("You are about to delete the list %s \nand all of its save bookmarks \n Are you sure?", _this->aaedcfg->selected->name.c_str());
                    if (ImGui::Button("Confirm")) {
                         _this->tempvals.bookmark=0;
                         if (!(_this->aaedcfg->selected->prev)) {
                              if (_this->aaedcfg->selected->next) {
                                   _this->aaedcfg->lists=_this->aaedcfg->selected->next;
                              }
                         }
                         _this->aaedcfg->selected = delete_list(_this->aaedcfg->selected);
                         _this->tempvals.bookmark=0;
                         saveConfig(_this->aaedcfg, NULL);
                         ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel")) {
                         ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
               }

               // displayed lists
               ImGui::SameLine();
               if (ImGui::Button(("Displayed##_aaed_mgr_displayed_lst_" + _this->name).c_str())) {
                    ImGui::OpenPopup("list_displayed_conf");
               }
               
               if (ImGui::BeginPopup("list_displayed_conf")) { 
                    if (ImGui::BeginTable(("list_manager_shown_table" + _this->name).c_str(), 1, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0, 200))) {
                         current_list= _this->aaedcfg->lists;
                         while (current_list) {
                              ImGui::TableNextRow();
                              ImGui::TableNextColumn();
                              ImGui::Checkbox(current_list->name.c_str(), &(current_list->shown));
                              current_list=current_list->next;
                         }
                         ImGui::EndTable();
                    }
                    if (ImGui::Button("Confirm")) {
                         saveConfig(_this->aaedcfg, NULL);
                         ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
               }
         
               // hidden list notification banner
               if (!(_this->aaedcfg->selected->shown)) {
                    ImGui::SameLine();
                    ImGui::Text("HIDDEN");
               }
            
  
  
  /// somewhere after this -------------------------------------------------------------
               // add bookmark button
               ImGui::Separator();
               if (ImGui::Button(("New##_aaed_mgr_new_bkm_" + _this->name).c_str())) {
                    struct frequency* new_bookmark;
                    new_bookmark = new struct frequency;
                    std::cout << "adding bookmark  \n";
                    if (_this->tempvals.bookmark) {
                         std::cout << "After bookmark" << _this->tempvals.bookmark->name.c_str()  << "\n";
                         new_bookmark->next = _this->tempvals.bookmark->next;
                         new_bookmark->prev = _this->tempvals.bookmark;
                         _this->tempvals.bookmark->next=new_bookmark;
                         if (new_bookmark->next) {
                              new_bookmark->next->prev=new_bookmark;
                         }
                         
                    } else {
                         std::cout << "at top of list" << _this->aaedcfg->selected->name.c_str()<< "\n";
                         new_bookmark->next = _this->aaedcfg->selected->frequencies;
                         new_bookmark->prev = 0;
                         std::cout << "set new bookmark pointers\n";
                         if (_this->aaedcfg->selected->frequencies) {
                              _this->aaedcfg->selected->frequencies->prev = new_bookmark;
                         }
                         _this->aaedcfg->selected->frequencies=new_bookmark;
                         std::cout << "set selected list pointers\n";
                    }
                    _this->tempvals.bookmark=new_bookmark;
                    new_bookmark->selected=false;
                    std::cout << "Populating frequency information from radio to new bookmark\n";
                    if (gui::waterfall.selectedVFO == "") {
                         core::modComManager.callInterface("Radio", RADIO_IFACE_CMD_GET_MODE, NULL, (int*)&(new_bookmark->mode));
                         new_bookmark->bandwidth=0;
                         new_bookmark->frequency =  gui::waterfall.getCenterFrequency();
                         new_bookmark->mode = (freq_mode)7;
                    } else { // radio selected
                         new_bookmark->frequency = gui::waterfall.getCenterFrequency() + sigpath::vfoManager.getOffset(gui::waterfall.selectedVFO);
                         new_bookmark->bandwidth = sigpath::vfoManager.getBandwidth(gui::waterfall.selectedVFO);
                         new_bookmark->mode = (freq_mode)7;
                         if (core::modComManager.getModuleName(gui::waterfall.selectedVFO) == "radio") {
                              int mode;
                              core::modComManager.callInterface(gui::waterfall.selectedVFO, RADIO_IFACE_CMD_GET_MODE, NULL, &mode);
                              new_bookmark->mode = (freq_mode)mode;
                         }
                    }               
                    std::cout << "Got radio stats, setting name\n";
                    new_bookmark->name= utils::formatFreq(new_bookmark->frequency);
                    std::cout << "Prepping for edit box\n";
                    _this->tempvals.bookmark=new_bookmark;
                    _this->tempvals.temp_freq.name=_this->tempvals.bookmark->name;
                    _this->tempvals.temp_freq.frequency=_this->tempvals.bookmark->frequency;
                    _this->tempvals.temp_freq.mode=_this->tempvals.bookmark->mode;
                    _this->tempvals.temp_freq.bandwidth=_this->tempvals.bookmark->bandwidth;
                    std::cout << "Opening edit box\n";
                    ImGui::OpenPopup("bkm_edit_conf");
               }
               bool bookmarkenable = false;
               if   (_this->tempvals.bookmark) {
                    bookmarkenable = true;
               }
               // EDIT BOOKMARK BUTTON
               if (!bookmarkenable) {
                    style::beginDisabled();
               }
               ImGui::SameLine();
               if (ImGui::Button(("Edit##_aaed_mgr_edit_bkm_" + _this->name).c_str())) {
                    _this->tempvals.temp_freq.name=_this->tempvals.bookmark->name;
                    _this->tempvals.temp_freq.frequency=_this->tempvals.bookmark->frequency;
                    _this->tempvals.temp_freq.mode=_this->tempvals.bookmark->mode;
                    _this->tempvals.temp_freq.bandwidth=_this->tempvals.bookmark->bandwidth;
                    ImGui::OpenPopup("bkm_edit_conf");
               }

               if (!bookmarkenable) {
                    style::endDisabled();
               }


               // bookmark edit dialog
               if (ImGui::BeginPopup("bkm_edit_conf")) {
                     // NAME BOX
                     strcpy (editNameBuf, _this->tempvals.temp_freq.name.c_str());
                     if (ImGui::InputText(("Name##aaed_mgr_edit_bm_name" + _this->name).c_str(), editNameBuf, 1024, 0, NULL)) {
                          _this->tempvals.temp_freq.name = editNameBuf;
                     }
                     // FREQUENCY BOX     
                     editFreqBuf = _this->tempvals.temp_freq.frequency;
                     if (ImGui::InputDouble(("Frequency##aaed_mgr_edit_bm_freq" + _this->name).c_str(), &editFreqBuf, 1, 1, "%.1f", 0)) {
                          _this->tempvals.temp_freq.frequency = editFreqBuf;
                     }
                     // BANDWIDTH BOX 
                     editBwBuf = _this->tempvals.temp_freq.bandwidth;
                     if (ImGui::InputDouble(("Bandwidth##aaed_mgr_edit_bm_bandwidth" + _this->name).c_str(), &editBwBuf)) {
                          _this->tempvals.temp_freq.bandwidth = editBwBuf;
                     }
             
                     // MODE SELECT COMBO BOX
                     bool is_mode_selected;
                     if ((int)(_this->tempvals.temp_freq.mode) < 8) {
                          if (ImGui::BeginCombo("##aaed_manager_bm_mode_sel", _this->ModeList[(int)(_this->tempvals.temp_freq.mode)].c_str())) {
                               is_mode_selected = (_this->tempvals.temp_freq.mode==MODE_NFM);
                               if (ImGui::Selectable(_this->ModeList[MODE_NFM].c_str(), is_mode_selected)) {
                                    _this->tempvals.temp_freq.mode = MODE_NFM;
                               }
                               is_mode_selected = (_this->tempvals.temp_freq.mode==MODE_WFM);
                               if (ImGui::Selectable(_this->ModeList[MODE_WFM].c_str(), is_mode_selected)) {
                                    _this->tempvals.temp_freq.mode = MODE_WFM;
                               }
                               is_mode_selected = (_this->tempvals.temp_freq.mode==MODE_AM);
                               if (ImGui::Selectable(_this->ModeList[MODE_AM].c_str(), is_mode_selected)) {
                                    _this->tempvals.temp_freq.mode = MODE_AM;
                               }
                               is_mode_selected = (_this->tempvals.temp_freq.mode==MODE_DSB);
                               if (ImGui::Selectable(_this->ModeList[MODE_DSB].c_str(), is_mode_selected)) {
                                    _this->tempvals.temp_freq.mode = MODE_DSB;
                               }
                               is_mode_selected = (_this->tempvals.bookmark->mode==MODE_USB);
                               if (ImGui::Selectable(_this->ModeList[MODE_USB].c_str(), is_mode_selected)) {
                                    _this->tempvals.temp_freq.mode = MODE_USB;
                               }
                               is_mode_selected = (_this->tempvals.bookmark->mode==MODE_LSB);
                               if (ImGui::Selectable(_this->ModeList[MODE_LSB].c_str(), is_mode_selected)) {
                                    _this->tempvals.temp_freq.mode = MODE_LSB;
                               }
                               is_mode_selected = (_this->tempvals.bookmark->mode==MODE_CW);
                               if (ImGui::Selectable(_this->ModeList[MODE_CW].c_str(), is_mode_selected)) {
                                    _this->tempvals.temp_freq.mode = MODE_CW;
                               }
                               is_mode_selected = (_this->tempvals.bookmark->mode==MODE_RAW);
                               if (ImGui::Selectable(_this->ModeList[MODE_RAW].c_str(), is_mode_selected)) {
                                    _this->tempvals.temp_freq.mode = MODE_RAW;
                               }
                               ImGui::EndCombo();
                          }
                     }

                     if (ImGui::Button("Confirm")) {
                          // apply the changes
                          _this->tempvals.bookmark->name = _this->tempvals.temp_freq.name;
                          _this->tempvals.bookmark->frequency = _this->tempvals.temp_freq.frequency;
                          _this->tempvals.bookmark->mode = _this->tempvals.temp_freq.mode;
                          _this->tempvals.bookmark->bandwidth = _this->tempvals.temp_freq.bandwidth;
                          saveConfig(_this->aaedcfg, NULL);
                          ImGui::CloseCurrentPopup();
                     }
                     
                     ImGui::SameLine();
                     if (ImGui::Button("Cancel")) {
                          ImGui::CloseCurrentPopup();
                     }
                     ImGui::EndPopup();
               }

               if (!bookmarkenable) {
                     style::beginDisabled();
               }

        
               // DELETE BOOKMARK BUTTON
               ImGui::SameLine();
               if (ImGui::Button(("Delete##_aaed_mgr_delete_bkm_" + _this->name).c_str())) {
                    ImGui::OpenPopup("book_delete_conf");
               }
               
               if (ImGui::BeginPopup("book_delete_conf")) {
                    ImGui::Text("You are about to delete the bookmark %s \nfrom list %s \n Are you sure?", _this->tempvals.bookmark->name.c_str(),_this->aaedcfg->selected->name.c_str());
                    if (ImGui::Button("Confirm")) {
                         struct frequency *temp;
                         temp= delete_bookmark(_this->tempvals.bookmark );
                         std::cout << "Entry deleted\n";
                         if ((!temp) || (!(temp->prev))) {
                              _this->aaedcfg->selected->frequencies=temp;
                         }
                         _this->tempvals.bookmark=_this->aaedcfg->selected->frequencies;
                         saveConfig(_this->aaedcfg, NULL);
                         std::cout << "Closing popup\n";
                         ImGui::CloseCurrentPopup();
                         
                    }
                    
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel")) {
                         ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
               }
               if (!bookmarkenable) {
                    style::endDisabled();
               }
         
               // BOOKMARK LIST TABLE
               
               if (ImGui::BeginTable(("freq_manager_bkm_table" + _this->name).c_str(), 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0, 200))) {
                    ImGui::TableSetupColumn("Name");
                    ImGui::TableSetupColumn("Freq", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                    ImGui::TableSetupColumn("Mode", ImGuiTableColumnFlags_WidthFixed, 40.0f);
                    ImGui::TableSetupScrollFreeze(3, 1);
                    ImGui::TableHeadersRow();
                    current_bookmark= _this->aaedcfg->selected->frequencies;
                    if  (current_bookmark) {
                         while (current_bookmark) {
                              ImGui::TableNextRow();
                              ImGui::TableNextColumn();
                              ImGui::Selectable((current_bookmark->name+"##bookmark_table_row").c_str(), &(current_bookmark->selected), (ImGuiSelectableFlags_SpanAllColumns||ImGuiSelectableFlags_AllowDoubleClick));

                              if (ImGui::IsMouseDoubleClicked(0) && ImGui::IsItemHovered()) {
                                   int mode = (int)(current_bookmark->mode);
                                   float bandwidth = current_bookmark->bandwidth;
                                   std::string selected_radio = gui::waterfall.selectedVFO;
                                   if (selected_radio.empty()) {
                                        selected_radio="Radio";
                                   }
                                   core::modComManager.callInterface(selected_radio.c_str(),  RADIO_IFACE_CMD_SET_MODE, &mode, NULL);
                                   core::modComManager.callInterface(selected_radio.c_str(),  RADIO_IFACE_CMD_SET_BANDWIDTH, &bandwidth, NULL);
                                   tuner::tune(tuner::TUNER_MODE_CENTER, selected_radio.c_str(), current_bookmark->frequency);
                              }
                              if (ImGui::IsMouseClicked(0) && ImGui::IsItemHovered()) {
                                  _this->tempvals.bookmark=current_bookmark;
                              }
                    
                              ImGui::TableNextColumn();
                              ImGui::Text("%s", utils::formatFreq(current_bookmark->frequency).c_str());
                              ImGui::TableNextColumn();
                              ImGui::Text("%s", _this->ModeList[(int)current_bookmark->mode].c_str());
                              current_bookmark=current_bookmark->next;
                         }
                    } 
                    ImGui::EndTable();
               }
               ImGui::Separator();

               // bookmark import
               if (ImGui::Button(("Import##_aaed_mgr_import_bkm_" + _this->name).c_str())) {
                    _this->importDialog = new pfd::open_file("Import bookmarks", "", { "JSON Files (*.json)", "*.json", "All Files", "*" }, pfd::opt::none);
               }
               ImGui::SameLine();
               
               // bookmark export
               if (ImGui::Button(("Export##_aaed_mgr_export_bkm_" + _this->name).c_str())) {
                    _this->exportDialog = new pfd::save_file("Export bookmarks", "", { "JSON Files (*.json)", "*.json", "All Files", "*" }, pfd::opt::none);
               }
               
               if (_this->importDialog) {
                    if (_this->importDialog->ready()) {
                         std::vector<std::string> paths = _this->importDialog->result();
                         if (paths.size() > 0 ) {
                              importBookmarks(_this->aaedcfg, paths[0]);
                              saveConfig(_this->aaedcfg, NULL);
                         }
                         delete _this->importDialog;
                         _this->importDialog=0;
                    }
               }
 
               if (_this->exportDialog) {
                    if (_this->exportDialog->ready()) {
                         std::string path = _this->exportDialog->result();
                         if (!path.empty()) {
                              saveConfig(_this->aaedcfg, &path);
                         }
                         delete _this->exportDialog;
                         _this->exportDialog=0;
                    }
               }
          }
     // end of private
};

MOD_EXPORT void _INIT_() {
     // Nothing here
}

MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) {
     return new aaed_freq_manager(name);
}

MOD_EXPORT void _DELETE_INSTANCE_(void* instance) {
     delete (aaed_freq_manager*)instance;
}

MOD_EXPORT void _END_() {
     // Nothing here
}
