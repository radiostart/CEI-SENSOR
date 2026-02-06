import sys
from PIL import Image, ImageOps, ImageFilter

# Image Path
IMG_PATH = "/Users/jay-p/.gemini/antigravity/brain/75336f2a-f46e-4d27-b509-ad45ea140fc7/uploaded_image_1765895517671.png"
OUTPUT_C_PATH = "src/digit_glyphs_32x64.c"

DIGIT_W = 32
DIGIT_H = 64

def generate_c_file():
    try:
        img_orig = Image.open(IMG_PATH)
        # Handle transparency: Create white background
        if img_orig.mode in ('RGBA', 'LA') or (img_orig.mode == 'P' and 'transparency' in img_orig.info):
            bg = Image.new('RGB', img_orig.size, (255, 255, 255))
            if img_orig.mode == 'P':
                img_orig = img_orig.convert('RGBA')
            bg.paste(img_orig, mask=img_orig.split()[-1]) # Use alpha channel as mask
            img = bg.convert('L')
        else:
            img = img_orig.convert('L')
            
        print(f"Image mode: {img_orig.mode}, Size: {img.size}")
        
    except Exception as e:
        print(f"Error opening image: {e}")
        sys.exit(1)
        
    # Stats
    stat = ImageOps.grayscale(img).getextrema()
    print(f"Extrema: {stat}")
    # Calculate mean
    hist = img.histogram()
    mean = sum(i * n for i, n in enumerate(hist)) / (img.width * img.height)
    print(f"Mean pixel value: {mean}")
    
    if mean > 128:
        print("Detected Light Background -> Inverting")
        img = ImageOps.invert(img)
    else:
        print("Detected Dark Background -> Keeping")
        
    # Binarize
    THRESHOLD = 128
    img = img.point(lambda p: 255 if p > THRESHOLD else 0)

    # Find blobs using simple projection or connected components?
    # Since we can't use cv2, we use recursive search or simpler projection
    # Horizontal projection to separate digits?
    # No, digits are likely arranged horizontally: "1 2 3 ... 9 0"
    
    # We can use a simple technique: scan X axis, find gaps.
    w, h = img.size
    pixels = img.load()
    
    # Histogram of vertical columns
    col_counts = [0] * w
    for x in range(w):
        count = 0
        for y in range(h):
            if pixels[x, y] > 128:
                count += 1
        col_counts[x] = count
        
    # Identify segments (digits)
    # A segment is a continuous range of columns where count > 0
    segments = []
    in_segment = False
    start_x = 0
    
    for x in range(w):
        if col_counts[x] > 0:
            if not in_segment:
                in_segment = True
                start_x = x
        else:
            if in_segment:
                in_segment = False
                segments.append((start_x, x)) # end is exclusive
                
    if in_segment:
        segments.append((start_x, w))
        
    print(f"Found {len(segments)} segments.")
    
    # If we found > 10 segments, we might have noise or fragmented digits.
    # Filter small segments (noise)
    # Calculate widths
    valid_segments = []
    min_width = 5 # arbitrary noise filter
    for s in segments:
        if (s[1] - s[0]) > min_width:
            valid_segments.append(s)
            
    segments = valid_segments
    print(f"Valid segments (digits): {len(segments)}")
    
    if len(segments) != 10:
        print("Warning: Did not find exactly 10 digits. Found:", len(segments))
        # If we have issues, we might proceed but mapping will be wrong.
        # But let's assume the image is clean enough.
        
    # Digits are 1, 2, ..., 9, 0
    # Map index to digit value
    digit_map = [1, 2, 3, 4, 5, 6, 7, 8, 9, 0]
    
    generated_arrays = {}
    
    for i, (sx, ex) in enumerate(segments):
        if i >= 10: break
        
        # Crop Digit
        digit_val = digit_map[i]
        
        # Refine vertical bounds
        # Scan Y to find top/bottom
        top, bottom = 0, h
        
        # Top
        for y in range(h):
            has_pixel = False
            for x in range(sx, ex):
                if pixels[x, y] > 128:
                    has_pixel = True
                    break
            if has_pixel:
                top = y
                break
                
        # Bottom
        for y in range(h-1, -1, -1):
            has_pixel = False
            for x in range(sx, ex):
                if pixels[x, y] > 128:
                    has_pixel = True
                    break
            if has_pixel:
                bottom = y + 1 # exclusive
                break
                
        # Safety
        if bottom <= top: bottom = top + 1
        
        crop = img.crop((sx, top, ex, bottom))
        
        # Resize to fit in 32x64
        # We want to preserve aspect ratio? 
        # The user said "This font...". Probably maximize size.
        # Let's scale to fit 32x64 fully? Or centered?
        # Let's try fitting while maintaining aspect ratio, but filling at least one dimension.
        cw, ch = crop.size
        
        # Target: 32x64
        # Scale factor
        scale_w = DIGIT_W / cw
        scale_h = DIGIT_H / ch
        scale = min(scale_w, scale_h) * 0.95 # 95% to leave slight margin? Or 1.0
        # User usually wants full size. Let's do 1.0 of min scale
        scale = min(scale_w, scale_h)
        
        new_w = int(cw * scale)
        new_h = int(ch * scale)
        
        resized = crop.resize((new_w, new_h), Image.Resampling.LANCZOS)
        
        # Threshold again after resize (grayscale interpolation)
        resized = resized.point(lambda p: 255 if p > 128 else 0)
        
        # Paste into 32x64 canvas (centered)
        canvas = Image.new('L', (DIGIT_W, DIGIT_H), 0)
        off_x = (DIGIT_W - new_w) // 2
        off_y = (DIGIT_H - new_h) // 2
        canvas.paste(resized, (off_x, off_y))
        
        # Generate Byte Array
        # 1bpp, row-major, MSB-first.
        # W=32 -> 4 bytes per row.
        # H=64
        
        byte_data = []
        c_pixels = canvas.load()
        
        for y in range(DIGIT_H):
            row_bytes = [0, 0, 0, 0]
            for x in range(DIGIT_W):
                if c_pixels[x, y] > 128:
                    # Set bit
                    byte_idx = x // 8
                    bit_idx = 7 - (x % 8)
                    row_bytes[byte_idx] |= (1 << bit_idx)
            byte_data.extend(row_bytes)
            
        generated_arrays[digit_val] = byte_data
        
    # Write C File
    with open(OUTPUT_C_PATH, "w") as f:
        f.write('#include "digit_glyphs_32x64.h"\n\n')
        
        for d in range(10):
            if d not in generated_arrays:
                # Fallback empty
                f.write(f"const uint8_t DIGIT_{d}_32x64[256] = {{0}};\n")
                continue
                
            data = generated_arrays[d]
            f.write(f"// Digit {d}\n")
            f.write(f"const uint8_t DIGIT_{d}_32x64[256] = {{\n")
            
            # Format nicely
            for i in range(0, len(data), 12): # 12 bytes per line
                chunk = data[i:i+12]
                hex_strs = [f"0x{b:02X}" for b in chunk]
                f.write("    " + ", ".join(hex_strs) + ",\n")
            
            f.write("};\n\n")
            
    print(f"Successfully wrote {OUTPUT_C_PATH}")

if __name__ == "__main__":
    generate_c_file()
