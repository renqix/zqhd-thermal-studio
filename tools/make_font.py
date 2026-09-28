"""从SIL OFL的Noto Sans CJK生成16像素字形；不读取系统字体。"""
from pathlib import Path
import argparse,pygame
p=argparse.ArgumentParser();p.add_argument('font',type=Path);a=p.parse_args()
pygame.font.init();font=pygame.font.Font(str(a.font),16)
chars=sorted(set('映射最低最高温度自定义开关确定取消等待数据探头无设置返回滤波平滑快速插值关闭线性三次发率亮度图像测与显示模式白黑热经典铁红彩虹熔岩翠绿暂停锁屏清晰取当前用手动启时增强L3'))
bitmap=[];glyphs=['{0,0,0,0,0,0}'];atlas=pygame.Surface((16*20,22*4));atlas.fill('black')
for index,char in enumerate(chars):
    surface=font.render(char,False,'white','black');mask=pygame.mask.from_surface(surface)
    # 黑色背景是不透明的，按亮度寻找笔画范围，而非按alpha求边界。
    points=[(x,y) for y in range(surface.get_height()) for x in range(surface.get_width()) if surface.get_at((x,y))[0]>127]
    top=min(y for x,y in points);bottom=max(y for x,y in points);left=min(x for x,y in points);right=max(x for x,y in points)
    assert right-left+1<=16 and bottom-top+1<=18,char
    canvas=pygame.Surface((16,18));canvas.fill('black');canvas.blit(surface,((16-surface.get_width())//2,(18-(bottom-top+1))//2-top))
    start=len(bitmap)
    for y in range(18):
        for byte in range(2):bitmap.append(sum((1<<(7-bit)) if canvas.get_at((byte*8+bit,y))[0]>127 else 0 for bit in range(8)))
    glyphs.append('{%d,256,16,18,0,-2}'%start);atlas.blit(canvas,((index%20)*16,(index//20)*22))
base=ord(chars[0]);codes=[ord(c)-base for c in chars]
s='// Noto Sans CJK字形子集，SIL OFL 1.1；生成：tools/make_font.py。\n#include <lvgl.h>\n'
s+='static const uint8_t bitmap[] = {'+','.join(map(str,bitmap))+'};\n'
s+='static const lv_font_fmt_txt_glyph_dsc_t glyphs[] = {'+','.join(glyphs)+'};\n'
s+='static const uint16_t unicode_list[] = {'+','.join(map(str,codes))+'};\n'
s+='static const lv_font_fmt_txt_cmap_t cmaps[] = {{.range_start=%d,.range_length=%d,.glyph_id_start=1,.unicode_list=unicode_list,.glyph_id_ofs_list=NULL,.list_length=%d,.type=LV_FONT_FMT_TXT_CMAP_SPARSE_TINY}};\n'%(base,codes[-1]+1,len(chars))
s+='''static lv_font_fmt_txt_glyph_cache_t cache;
static lv_font_fmt_txt_dsc_t desc = {.glyph_bitmap=bitmap,.glyph_dsc=glyphs,.cmaps=cmaps,.kern_dsc=NULL,.kern_scale=0,.cmap_num=1,.bpp=1,.kern_classes=0,.bitmap_format=0,.cache=&cache};
const lv_font_t thermal_cn_font = {.get_glyph_dsc=lv_font_get_glyph_dsc_fmt_txt,.get_glyph_bitmap=lv_font_get_bitmap_fmt_txt,.line_height=18,.base_line=2,.subpx=LV_FONT_SUBPX_NONE,.underline_position=-1,.underline_thickness=1,.dsc=&desc};
'''
root=Path(__file__).resolve().parents[1];(root/'firmware/thermal_v2/thermal_cn_font.c').write_text(s);pygame.image.save(atlas,str(root/'docs/font-preview.png'))
