#include "raylib.h"
#include "orion/colors.h"
#include "orion/language.h"
#include "orion/lexer.h"
#include "orion/listing.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define WINDOW_W 1440
#define WINDOW_H 900
#define ROW_H 25
#define MAX_LINE 2048
#define PANELS 4

typedef struct { char **items; int *line; ASTNode **node; size_t count, cap; } Rows;
static const char *token_names[TOKEN_MAX] = {
#define X(name, str, cat) [name] = #name,
#include "orion/tokens.def.h"
#undef X
};
static const char *ast_names[AST_MAX] = {
#define X(name, body, cond, expr) [name] = #name,
#include "orion/ast.def.h"
#undef X
};
static const char *titles[PANELS] = {"SOURCE", "LEXER  /  TOKENS", "PARSER  /  AST", "CODEGEN  /  INSTRUCTIONS"};
static const char *notes[PANELS] = {"Orion source", "Characters grouped into tokens", "Tokens connected into syntax", "Instruction emitted by selected AST node"};
static Color accents[PANELS] = {ORI_COLOR_FLOW_ACTIVE,ORI_COLOR_FLOW_IF,ORI_COLOR_FLOW_CALL,ORI_COLOR_FLOW_DECL};

static void push(Rows *r,const char *text,int line,ASTNode *node){
    if(r->count==r->cap){size_t cap=r->cap?r->cap*2:64;char **items=realloc(r->items,cap*sizeof(*items));if(!items)return;r->items=items;int *lines=realloc(r->line,cap*sizeof(*lines));if(!lines)return;r->line=lines;ASTNode **nodes=realloc(r->node,cap*sizeof(*nodes));if(!nodes)return;r->node=nodes;r->cap=cap;}
    r->items[r->count]=strdup(text?text:"");r->line[r->count]=line;r->node[r->count++]=node;
}
static char *read_file(const char *path){FILE *f=fopen(path,"rb");if(!f){perror(path);return NULL;}if(fseek(f,0,SEEK_END)){fclose(f);return NULL;}long n=ftell(f);if(n<0||fseek(f,0,SEEK_SET)){fclose(f);return NULL;}char *s=malloc((size_t)n+1);if(!s){fclose(f);return NULL;}size_t got=fread(s,1,(size_t)n,f);s[got]=0;fclose(f);return s;}
static void push_source(Rows *r,const char *text){const char *start=text;int line=1;for(const char *p=text;;p++)if(*p=='\n'||!*p){size_t n=(size_t)(p-start);char *s=malloc(n+1);if(s){memcpy(s,start,n);s[n]=0;push(r,s,line++,NULL);free(s);}if(!*p)break;start=p+1;}}
static void ast_rows(Rows *r,ASTNode *n,int depth,const char *edge){for(;n;n=n->next){char b[MAX_LINE];const char *type=(n->type>=0&&n->type<AST_MAX&&ast_names[n->type])?ast_names[n->type]:"AST_UNKNOWN";snprintf(b,sizeof(b),"%*s%s%s  %s%s%s%s  @%d:%d",depth*2,"",edge?edge:"",edge?" ":"",type,n->opToken.lexeme?" \"":"",n->opToken.lexeme?n->opToken.lexeme:"",n->opToken.lexeme?"\"":"",n->opToken.line,n->opToken.column);push(r,b,n->opToken.line,n);if(n->left)ast_rows(r,n->left,depth+1,"left:");if(n->right)ast_rows(r,n->right,depth+1,"right:");if(n->extra)ast_rows(r,n->extra,depth+1,"extra:");}}
static void free_rows(Rows *r){for(size_t i=0;i<r->count;i++)free(r->items[i]);free(r->items);free(r->line);free(r->node);}
static float clampf(float v,float lo,float hi){return v<lo?lo:v>hi?hi:v;}
static float max_scroll(size_t count,Rectangle box){float n=(float)count*ROW_H-(box.height-43);return n>0?n:0;}
static Rectangle body_of(Rectangle box){return (Rectangle){box.x+5,box.y+37,box.width-18,box.height-43};}
static int token_for_ast(Token *tokens,int token_count,int line,const char *node_text){for(int i=0;i<token_count;i++)if(tokens[i].line==line&&node_text&&tokens[i].lexeme&&strstr(node_text,tokens[i].lexeme))return i;for(int i=0;i<token_count;i++)if(tokens[i].line==line)return i;return token_count?0:-1;}
static int token_for_node(Token *tokens,int count,ASTNode *node){if(!node)return -1;for(int i=0;i<count;i++)if(tokens[i].line==node->opToken.line&&tokens[i].lexeme&&node->opToken.lexeme&&!strcmp(tokens[i].lexeme,node->opToken.lexeme))return i;return token_for_ast(tokens,count,node->opToken.line,NULL);}
static int operand_is(const char *text,const char *name){if(!name)return 0;while(*text==' '||*text=='\t')text++;size_t n=strcspn(text," \t;");return strlen(name)==n&&!strncmp(text,name,n);}
static int node_matches_instruction(ASTNode *n,const char *instruction){
    while(*instruction==' '||*instruction=='\t')instruction++;
    if(!strncmp(instruction,"PUSH_I64 ",9)||!strncmp(instruction,"PUSH_F64 ",9)||!strncmp(instruction,"PUSH_STRING ",12)){const char *v=strchr(instruction,' ');return n->type==AST_LITERAL&&v&&operand_is(v+1,n->opToken.lexeme);}
    if(!strncmp(instruction,"PUSH_NULL",9))return n->type==AST_LITERAL&&n->opToken.type==TOKEN_NULL;
    if(!strncmp(instruction,"FUNC ",5))return n->type==AST_FUNC_DECL&&operand_is(instruction+5,n->opToken.lexeme);
    if(!strncmp(instruction,"STRUCT ",7))return n->type==AST_STRUCT_DECL&&operand_is(instruction+7,n->opToken.lexeme);
    if(!strncmp(instruction,"FIELD ",6))return n->type==AST_FIELD_DECL&&operand_is(instruction+6,n->opToken.lexeme);
    if(!strncmp(instruction,"LOAD_LOCAL ",11))return n->type==AST_IDENT&&operand_is(instruction+11,n->opToken.lexeme);
    if(!strncmp(instruction,"DECLARE_LOCAL ",14))return n->type==AST_VAR_DECL&&operand_is(instruction+14,n->opToken.lexeme);
    if(!strncmp(instruction,"STORE_LOCAL ",12))return n->type==AST_ASSIGN&&n->left&&n->left->type==AST_IDENT&&operand_is(instruction+12,n->left->opToken.lexeme);
    if(!strncmp(instruction,"BINARY_",7))return n->type==AST_BINARY&&n->opToken.lexeme&&!strcmp(instruction+7,n->opToken.lexeme);
    if(!strncmp(instruction,"UNARY_",6))return n->type==AST_UNARY&&n->opToken.lexeme&&!strcmp(instruction+6,n->opToken.lexeme);
    if(!strncmp(instruction,"CALL ",5))return n->type==AST_CALL&&operand_is(instruction+5,n->opToken.lexeme);
    if(!strncmp(instruction,"PARAM ",6))return n->type==AST_IDENT&&n->extra&&operand_is(instruction+6,n->opToken.lexeme);
    if(!strncmp(instruction,"ALLOC",5))return n->type==AST_ALLOC;
    if(!strncmp(instruction,"LOAD_ELEM",9)||!strncmp(instruction,"STORE_ELEM",10))return n->type==AST_INDEX_ACCESS;
    if(!strncmp(instruction,"LOAD_FIELD ",11))return n->type==AST_FIELD_ACCESS&&operand_is(instruction+11,n->opToken.lexeme);
    if(!strncmp(instruction,"STORE_FIELD ",12))return n->type==AST_FIELD_ACCESS&&operand_is(instruction+12,n->opToken.lexeme);
    if(!strncmp(instruction,"JUMP_IF_FALSE",13))return n->type==AST_IF_STMT||n->type==AST_WHILE_STMT;
    if(!strncmp(instruction,"RETURN",6))return n->type==AST_RETURN;
    if(!strncmp(instruction,"FREE",4))return n->type==AST_FREE;
    if(!strncmp(instruction,"POP",3))return n->type==AST_CALL;
    if(!strncmp(instruction,"JUMP",4))return n->type==AST_BREAK||n->type==AST_CONTINUE||n->type==AST_WHILE_STMT||n->type==AST_IF_STMT;
    return 0;
}
static int find_codegen_ast(Rows *ast,const char *instruction,int line){int fallback=-1;for(size_t i=0;i<ast->count;i++){ASTNode *n=ast->node[i];if(!n||n->opToken.line!=line)continue;if(fallback<0)fallback=(int)i;if(node_matches_instruction(n,instruction))return (int)i;}for(size_t i=0;i<ast->count;i++)if(ast->node[i]&&node_matches_instruction(ast->node[i],instruction))return (int)i;return fallback;}
static int step_forward(size_t *lexed,size_t *parsed,size_t *emitted,Rows *rows,int *kind,int *index,float *anim,float *pulse){if(*lexed<rows[1].count){*kind=1;*index=(int)(*lexed)++;*anim=0;}else if(*parsed<rows[2].count){*kind=2;*index=(int)(*parsed)++;*anim=0;}else if(*emitted<rows[3].count){*kind=3;*index=(int)(*emitted)++;*anim=0;}else return 0;*pulse=1;return 1;}
static int step_back(size_t *lexed,size_t *parsed,size_t *emitted,int *kind,int *index,float *anim,float *pulse){if(*emitted){*kind=3;*index=(int)--(*emitted);*anim=1;}else if(*parsed){*kind=2;*index=(int)--(*parsed);*anim=1;}else if(*lexed){*kind=1;*index=(int)--(*lexed);*anim=1;}else return 0;*pulse=1;return 1;}
static void ensure_visible(float *scroll,size_t index,size_t count,Rectangle panel){Rectangle body=body_of(panel);float view=body.height;float top=(float)index*ROW_H,bottom=top+ROW_H;if(top<*scroll)*scroll=top;else if(bottom>*scroll+view)*scroll=bottom-view;*scroll=clampf(*scroll,0,max_scroll(count,panel));}
static void draw_panel(int id,Rectangle box,Rows *rows,size_t shown,float *scroll,int highlight,float pulse,Font font,Vector2 mouse,float wheel,int *drag_panel,float *drag_y){
    DrawRectangleRounded(box,0.025f,4,ORI_COLOR_PANEL);DrawRectangleRoundedLines(box,0.025f,4,ORI_COLOR_BUTTON_BORDER);
    DrawRectangle((int)box.x+1,(int)box.y+1,(int)box.width-2,31,Fade(accents[id],0.25f));
    DrawTextEx(font,titles[id],(Vector2){box.x+10,box.y+5},17,0.2f,ORI_COLOR_BRIGHT_TEXT);
    DrawTextEx(font,notes[id],(Vector2){box.x+10,box.y+19},11,0.15f,ORI_COLOR_MUTED_TEXT);
    Rectangle body=body_of(box),track={box.x+box.width-10,body.y,6,body.height};
    float max=max_scroll(shown,box);if(CheckCollisionPointRec(mouse,body)&&wheel!=0)*scroll-=wheel*ROW_H*3;*scroll=clampf(*scroll,0,max);
    if(IsMouseButtonPressed(MOUSE_BUTTON_LEFT)&&CheckCollisionPointRec(mouse,track)){*drag_panel=id;*drag_y=mouse.y;}
    if(IsMouseButtonReleased(MOUSE_BUTTON_LEFT))*drag_panel=-1;
    if(*drag_panel==id&&max>0){*scroll=clampf(*scroll+(mouse.y-*drag_y)*max/body.height,0,max);*drag_y=mouse.y;}
    float th=max>0?body.height*body.height/(body.height+max):body.height;if(th<24)th=24;float ty=track.y+(body.height-th)*(max>0?*scroll/max:0);
    DrawRectangleRec(track,Fade(ORI_COLOR_BACKGROUND,0.8f));DrawRectangleRounded((Rectangle){track.x-2,ty,10,th},0.6f,5,Fade(ORI_COLOR_MUTED_TEXT,0.85f));
    BeginScissorMode((int)body.x,(int)body.y,(int)body.width,(int)body.height);
    size_t first=(size_t)(*scroll/ROW_H);float off=-(float)((int)*scroll%ROW_H);
    for(size_t i=first;i<shown&&i<rows->count;i++){float y=body.y+off+(float)(i-first)*ROW_H;if(y>body.y+body.height)break;int on=(int)i==highlight;if(on)DrawRectangle((int)body.x,y-1,(int)body.width,ROW_H,Fade(accents[id],0.28f+0.24f*pulse));char num[16];snprintf(num,sizeof(num),"%3zu",i+1);DrawTextEx(font,num,(Vector2){body.x+3,y+2},13,0.1f,ORI_COLOR_FAINT_TEXT);DrawTextEx(font,rows->items[i],(Vector2){body.x+35,y+2},id==0?15:14,0.1f,on?ORI_COLOR_BRIGHT_TEXT:ORI_COLOR_TEXT);}
    EndScissorMode();
}
static void draw_arrow(Vector2 a,Vector2 b,Color c){DrawLineEx(a,b,2,c);Vector2 d={b.x-a.x,b.y-a.y};float len=sqrtf(d.x*d.x+d.y*d.y);if(len<1)return;d.x/=len;d.y/=len;Vector2 side={-d.y,d.x};Vector2 p1={b.x-d.x*10+side.x*5,b.y-d.y*10+side.y*5},p2={b.x-d.x*10-side.x*5,b.y-d.y*10-side.y*5};DrawTriangle(b,p1,p2,c);}
static void draw_flying(Font font,Vector2 from,Vector2 to,const char *label,float t,Color color){float e=t*t*(3-2*t);Vector2 p={from.x+(to.x-from.x)*e,from.y+(to.y-from.y)*e};Vector2 size=MeasureTextEx(font,label,14,0.1f);Rectangle r={p.x-8,p.y-5,size.x+16,24};DrawRectangleRounded(r,0.18f,4,ORI_COLOR_BACKGROUND);DrawRectangleRoundedLines(r,0.18f,4,color);DrawTextEx(font,label,(Vector2){p.x,p.y},14,0.1f,ORI_COLOR_BRIGHT_TEXT);}

int main(int argc,char **argv){
    if(argc!=2){fprintf(stderr,"Usage: %s file.ori\n",argv[0]);return 2;}
    char *source=read_file(argv[1]);if(!source)return 1;TokenStream tokens=lex_source(source);Parser parser={.tokens=tokens.data,.current=0,.token_count=tokens.count,.loop_depth=0};ASTNode *program=parse_program(&parser);char *listing=generate_bytecode_listing(program);if(!listing){fprintf(stderr,"Meissa: unable to generate instruction listing\n");return 1;}
    Rows rows[PANELS]={0};push_source(&rows[0],source);
    for(int i=0;i<tokens.count;i++){Token t=tokens.data[i];char b[MAX_LINE];const char *type=(t.type>=0&&t.type<TOKEN_MAX&&token_names[t.type])?token_names[t.type]:"TOKEN_UNKNOWN";snprintf(b,sizeof(b),"%s  %s",type,t.lexeme?t.lexeme:"");push(&rows[1],b,t.line,NULL);}
    ast_rows(&rows[2],program,0,NULL);char *save=NULL;int source_line=0;for(char *s=strtok_r(listing,"\n",&save);s;s=strtok_r(NULL,"\n",&save)){int mark;if(sscanf(s,";@ %d",&mark)==1){source_line=mark;continue;}push(&rows[3],s,source_line,NULL);}
    int *code_ast=malloc(rows[3].count*sizeof(*code_ast));if(code_ast)for(size_t i=0;i<rows[3].count;i++)code_ast[i]=find_codegen_ast(&rows[2],rows[3].items[i],rows[3].line[i]);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE|FLAG_MSAA_4X_HINT);InitWindow(WINDOW_W,WINDOW_H,"Meissa | Orion Compilation Pipeline");SetWindowMinSize(1000,650);SetTargetFPS(60);Font font=LoadFontEx("assets/NotoSans-VariableFont.ttf",22,NULL,0);if(!font.texture.id)font=GetFontDefault();SetTextureFilter(font.texture,TEXTURE_FILTER_BILINEAR);

    size_t lexed=0,parsed=0,emitted=0;int anim_kind=0,anim_index=0;float anim=1,pulse=0;float scroll[PANELS]={0};int drag_panel=-1;float drag_y=0;const char *filename=GetFileName(argv[1]);int playing=0;float steps_per_second=2.0f,play_clock=0,key_n_clock=0,key_b_clock=0;
    while(!WindowShouldClose()){
        int w=GetScreenWidth(),h=GetScreenHeight();Vector2 mouse=GetMousePosition();float dt=GetFrameTime();float wheel=GetMouseWheelMove();
        int stepped=0;int n_down=IsKeyDown(KEY_N)||IsKeyDown(KEY_RIGHT)||IsKeyDown(KEY_SPACE),b_down=IsKeyDown(KEY_B)||IsKeyDown(KEY_LEFT);
        if(IsKeyPressed(KEY_N)||IsKeyPressed(KEY_RIGHT)||IsKeyPressed(KEY_SPACE)){stepped=step_forward(&lexed,&parsed,&emitted,rows,&anim_kind,&anim_index,&anim,&pulse);key_n_clock=0.38f;}else if(n_down){key_n_clock-=dt;if(key_n_clock<=0){stepped=step_forward(&lexed,&parsed,&emitted,rows,&anim_kind,&anim_index,&anim,&pulse);key_n_clock=0.075f;}}else key_n_clock=0;
        if(IsKeyPressed(KEY_B)||IsKeyPressed(KEY_LEFT)){stepped=step_back(&lexed,&parsed,&emitted,&anim_kind,&anim_index,&anim,&pulse)||stepped;key_b_clock=0.38f;}else if(b_down){key_b_clock-=dt;if(key_b_clock<=0){stepped=step_back(&lexed,&parsed,&emitted,&anim_kind,&anim_index,&anim,&pulse)||stepped;key_b_clock=0.075f;}}else key_b_clock=0;
        Rectangle back_btn={24,(float)h-59,90,32},next_btn={122,(float)h-59,90,32},play_btn={220,(float)h-59,104,32},slower_btn={334,(float)h-59,30,32},faster_btn={433,(float)h-59,30,32};
        if(IsKeyPressed(KEY_P))playing=!playing;
        if(IsKeyPressed(KEY_EQUAL)||IsKeyPressed(KEY_KP_ADD))steps_per_second=clampf(steps_per_second+0.5f,0.5f,20.0f);
        if(IsKeyPressed(KEY_MINUS)||IsKeyPressed(KEY_KP_SUBTRACT))steps_per_second=clampf(steps_per_second-0.5f,0.5f,20.0f);
        if(IsMouseButtonPressed(MOUSE_BUTTON_LEFT)&&CheckCollisionPointRec(mouse,next_btn))stepped=step_forward(&lexed,&parsed,&emitted,rows,&anim_kind,&anim_index,&anim,&pulse)||stepped;
        if(IsMouseButtonPressed(MOUSE_BUTTON_LEFT)&&CheckCollisionPointRec(mouse,back_btn))stepped=step_back(&lexed,&parsed,&emitted,&anim_kind,&anim_index,&anim,&pulse)||stepped;
        if(IsMouseButtonPressed(MOUSE_BUTTON_LEFT)&&CheckCollisionPointRec(mouse,play_btn)){playing=!playing;play_clock=0;}
        if(IsMouseButtonPressed(MOUSE_BUTTON_LEFT)&&CheckCollisionPointRec(mouse,slower_btn))steps_per_second=clampf(steps_per_second-0.5f,0.5f,20.0f);
        if(IsMouseButtonPressed(MOUSE_BUTTON_LEFT)&&CheckCollisionPointRec(mouse,faster_btn))steps_per_second=clampf(steps_per_second+0.5f,0.5f,20.0f);
        if(playing&&!stepped){play_clock+=dt;float interval=1.0f/steps_per_second;while(play_clock>=interval){if(!step_forward(&lexed,&parsed,&emitted,rows,&anim_kind,&anim_index,&anim,&pulse)){playing=0;play_clock=0;break;}play_clock-=interval;stepped=1;if(play_clock<interval)break;}}
        anim=clampf(anim+dt*2.3f,0,1);pulse=clampf(pulse-dt*1.4f,0,1);
        Rectangle p[PANELS];float x=24,top=112,gap=20,total=w-48-gap*3;float widths[]={total*0.32f,total*0.20f,total*0.24f,total*0.24f};float ph=h-top-190;for(int i=0;i<PANELS;i++){p[i]=(Rectangle){x,top,widths[i],ph};x+=widths[i]+gap;}
        if(stepped&&anim_kind==1&&anim_index>=0&&anim_index<tokens.count){ensure_visible(&scroll[0],(size_t)(tokens.data[anim_index].line>0?tokens.data[anim_index].line-1:0),rows[0].count,p[0]);ensure_visible(&scroll[1],(size_t)anim_index,lexed,p[1]);}
        if(stepped&&anim_kind==2&&anim_index>=0&&(size_t)anim_index<rows[2].count){int ti=token_for_ast(tokens.data,tokens.count,rows[2].line[anim_index],rows[2].items[anim_index]);if(ti>=0)ensure_visible(&scroll[1],(size_t)ti,lexed,p[1]);ensure_visible(&scroll[2],(size_t)anim_index,parsed,p[2]);}
        if(stepped&&anim_kind==3&&anim_index>=0&&(size_t)anim_index<rows[3].count){int ai=code_ast?code_ast[anim_index]:-1;if(ai>=0)ensure_visible(&scroll[2],(size_t)ai,parsed,p[2]);ensure_visible(&scroll[3],(size_t)anim_index,emitted,p[3]);}
        size_t visible[PANELS]={rows[0].count,lexed,parsed,emitted};int active_line=0,active_token=-1,active_ast=-1,active_code=-1;
        if(anim_kind==1&&anim_index>=0&&anim_index<tokens.count){active_line=tokens.data[anim_index].line;active_token=anim_index;}
        else if(anim_kind==2&&anim_index>=0&&(size_t)anim_index<rows[2].count){active_line=rows[2].line[anim_index];active_ast=anim_index;}
        else if(anim_kind==3&&anim_index>=0&&(size_t)anim_index<rows[3].count){active_line=rows[3].line[anim_index];active_code=anim_index;active_ast=code_ast?code_ast[anim_index]:-1;}
        int source_high=-1;for(size_t i=0;i<rows[0].count;i++)if(rows[0].line[i]==active_line&&active_line>0){source_high=(int)i;break;}
        int token_high=active_token;
        if((anim_kind==2||anim_kind==3)&&active_ast>=0){token_high=token_for_node(tokens.data,tokens.count,rows[2].node[active_ast]);}
        int ast_high=active_ast;
        int code_high=active_code;
        BeginDrawing();ClearBackground(ORI_COLOR_BACKGROUND);DrawTextEx(font,"MEISSA",(Vector2){24,12},29,0.3f,ORI_COLOR_BRIGHT_TEXT);DrawTextEx(font,"Follow each lexeme as it becomes a token, joins the AST, and emits an instruction.",(Vector2){152,20},16,0.1f,ORI_COLOR_MUTED_TEXT);DrawTextEx(font,filename?filename:argv[1],(Vector2){(float)w-MeasureText(filename?filename:argv[1],16)-24,22},16,0.1f,ORI_COLOR_MUTED_TEXT);
        for(int i=0;i<PANELS;i++)draw_panel(i,p[i],&rows[i],visible[i],&scroll[i],i==0?source_high:i==1?token_high:i==2?ast_high:code_high,pulse,font,mouse,wheel,&drag_panel,&drag_y);
        if(anim_kind==1&&anim_index>=0&&anim_index<tokens.count){Token t=tokens.data[anim_index];if(t.line>0&&(size_t)(t.line-1)<rows[0].count){const char *line=rows[0].items[t.line-1];size_t col=t.column>0?(size_t)t.column-1:0;if(col>strlen(line))col=strlen(line);char prefix[MAX_LINE];snprintf(prefix,sizeof(prefix),"%.*s",(int)col,line);float sx=body_of(p[0]).x+35+MeasureTextEx(font,prefix,15,0.1f).x;float sy=body_of(p[0]).y+2+(float)(t.line-1)*ROW_H-scroll[0];float sw=MeasureTextEx(font,t.lexeme?t.lexeme:"",15,0.1f).x;DrawRectangleLinesEx((Rectangle){sx-2,sy-1,sw+4,ROW_H-2},2,accents[1]);}}
        for(int i=0;i<3;i++){Vector2 a={p[i].x+p[i].width+2,p[i].y+24},b={p[i+1].x-4,p[i+1].y+24};Color c=(anim_kind==i+1)?accents[i+1]:Fade(ORI_COLOR_MUTED_TEXT,0.55f);draw_arrow(a,b,c);}
        Vector2 from={0},to={0};char flight[MAX_LINE];int flying=0;
        if(anim<1&&anim_kind==1&&anim_index>=0&&anim_index<tokens.count){Token t=tokens.data[anim_index];if(t.line>0&&(size_t)(t.line-1)<rows[0].count){Rectangle sb=body_of(p[0]);float sy=sb.y+3+(float)(t.line-1)*ROW_H-scroll[0];const char *line=rows[0].items[t.line-1];size_t col=t.column>0?(size_t)t.column-1:0;if(col>strlen(line))col=strlen(line);char prefix[MAX_LINE];snprintf(prefix,sizeof(prefix),"%.*s",(int)col,line);float off=MeasureTextEx(font,prefix,15,0.1f).x;from=(Vector2){sb.x+35+off,sy+4};}Rectangle tb=body_of(p[1]);float ty=tb.y+3+(float)anim_index*ROW_H-scroll[1];to=(Vector2){tb.x+53,ty+4};const char *type=(t.type>=0&&t.type<TOKEN_MAX&&token_names[t.type])?token_names[t.type]:"TOKEN";snprintf(flight,sizeof(flight),"%s  →  %s",t.lexeme?t.lexeme:"",type);flying=1;}
        else if(anim<1&&anim_kind==2&&anim_index>=0&&(size_t)anim_index<rows[2].count){int ti=token_for_ast(tokens.data,tokens.count,rows[2].line[anim_index],rows[2].items[anim_index]);Rectangle tb=body_of(p[1]),ab=body_of(p[2]);from=(Vector2){tb.x+100,tb.y+3+(float)(ti<0?0:ti)*ROW_H-scroll[1]+4};to=(Vector2){ab.x+53,ab.y+3+(float)anim_index*ROW_H-scroll[2]+4};snprintf(flight,sizeof(flight),"%s",rows[2].items[anim_index]);flying=1;}
        else if(anim<1&&anim_kind==3&&anim_index>=0&&(size_t)anim_index<rows[3].count){int ai=code_ast?code_ast[anim_index]:-1;if(ai<0)ai=0;Rectangle ab=body_of(p[2]),cb=body_of(p[3]);from=(Vector2){ab.x+80,ab.y+3+(float)ai*ROW_H-scroll[2]+4};to=(Vector2){cb.x+45,cb.y+3+(float)anim_index*ROW_H-scroll[3]+4};snprintf(flight,sizeof(flight),"%s",rows[3].items[anim_index]);flying=1;}
        if(flying)draw_flying(font,from,to,flight,anim,accents[anim_kind]);
        DrawRectangleRounded(back_btn,0.16f,4,ORI_COLOR_BUTTON_DISABLED);DrawRectangleRoundedLines(back_btn,0.16f,4,ORI_COLOR_BUTTON_BORDER);DrawTextEx(font,"B  BACK",(Vector2){back_btn.x+12,back_btn.y+6},16,0.1f,ORI_COLOR_TEXT);
        DrawRectangleRounded(next_btn,0.16f,4,ORI_COLOR_BUTTON_ACTIVE);DrawRectangleRoundedLines(next_btn,0.16f,4,ORI_COLOR_BUTTON_BORDER);DrawTextEx(font,"N  NEXT",(Vector2){next_btn.x+12,next_btn.y+6},16,0.1f,ORI_COLOR_STEP_TEXT);
        DrawRectangleRounded(play_btn,0.16f,4,playing?ORI_COLOR_BUTTON_ACTIVE:ORI_COLOR_BUTTON_DISABLED);DrawRectangleRoundedLines(play_btn,0.16f,4,ORI_COLOR_BUTTON_BORDER);DrawTextEx(font,playing?"II  PAUSE":"PLAY",(Vector2){play_btn.x+11,play_btn.y+6},16,0.1f,ORI_COLOR_TEXT);
        DrawRectangleRounded(slower_btn,0.16f,4,ORI_COLOR_BUTTON_DISABLED);DrawRectangleRoundedLines(slower_btn,0.16f,4,ORI_COLOR_BUTTON_BORDER);DrawTextEx(font,"-",(Vector2){slower_btn.x+8,slower_btn.y+3},21,0.1f,ORI_COLOR_TEXT);
        char rate[48];snprintf(rate,sizeof(rate),"%.1f steps/s",steps_per_second);DrawTextEx(font,rate,(Vector2){374,(float)h-52},15,0.1f,ORI_COLOR_MUTED_TEXT);
        DrawRectangleRounded(faster_btn,0.16f,4,ORI_COLOR_BUTTON_DISABLED);DrawRectangleRoundedLines(faster_btn,0.16f,4,ORI_COLOR_BUTTON_BORDER);DrawTextEx(font,"+",(Vector2){faster_btn.x+8,faster_btn.y+5},18,0.1f,ORI_COLOR_TEXT);
        char status[256];snprintf(status,sizeof(status),"LEXER %zu/%zu   AST %zu/%zu   CODEGEN %zu/%zu",lexed,rows[1].count,parsed,rows[2].count,emitted,rows[3].count);DrawTextEx(font,status,(Vector2){478,(float)h-52},14,0.1f,ORI_COLOR_MUTED_TEXT);DrawTextEx(font,"Hold N/B to repeat    P toggles playback    +/− changes speed    Scroll each column independently",(Vector2){24,(float)h-19},13,0.1f,ORI_COLOR_FAINT_TEXT);
        EndDrawing();
    }
    UnloadFont(font);CloseWindow();free(code_ast);for(int i=0;i<PANELS;i++)free_rows(&rows[i]);free(source);free(listing);free_token_stream(&tokens);return 0;
}
