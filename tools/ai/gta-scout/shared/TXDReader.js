import { ChunkType, RasterFormat, D3DFormat, PlatformID, getChunkName } from './ChunkType.js';

export class TXDReader {
    constructor() {
        this.data = null;
        this.position = 0;
    }

    parse(arrayBuffer) {
        this.data = new DataView(arrayBuffer);
        this.position = 0;
        this.length = arrayBuffer.byteLength;

        const header = this.readHeader();
        if (header.type !== ChunkType.CHUNK_TEXDICTIONARY) {
            throw new Error('Not a valid TXD file');
        }

        return this.readTextureDictionary(header);
    }

    readHeader() {
        const header = {};
        header.type = this.readUInt32();
        header.name = getChunkName(header.type);
        header.length = this.readUInt32();
        header.build = this.readUInt32();

        if (header.build & 0xFFFF0000) {
            header.version = ((header.build >> 14) & 0x3FF00) | ((header.build >> 16) & 0x3F) | 0x30000;
        } else {
            header.version = header.build << 8;
        }

        return header;
    }

    readInt32() {
        if (this.position + 4 > this.length) {
            throw new Error(`TXDReader: Out of bounds reading Int32 at position ${this.position}, length ${this.length}`);
        }
        const v = this.data.getInt32(this.position, true);
        this.position += 4;
        return v;
    }

    readUInt32() {
        if (this.position + 4 > this.length) {
            throw new Error(`TXDReader: Out of bounds reading UInt32 at position ${this.position}, length ${this.length}`);
        }
        const v = this.data.getUint32(this.position, true);
        this.position += 4;
        return v;
    }

    readUInt16() {
        if (this.position + 2 > this.length) {
            throw new Error(`TXDReader: Out of bounds reading UInt16 at position ${this.position}, length ${this.length}`);
        }
        const v = this.data.getUint16(this.position, true);
        this.position += 2;
        return v;
    }

    readUInt8() {
        if (this.position + 1 > this.length) {
            throw new Error(`TXDReader: Out of bounds reading UInt8 at position ${this.position}, length ${this.length}`);
        }
        const v = this.data.getUint8(this.position);
        this.position += 1;
        return v;
    }

    readFloat32() {
        if (this.position + 4 > this.length) {
            throw new Error(`TXDReader: Out of bounds reading Float32 at position ${this.position}, length ${this.length}`);
        }
        const v = this.data.getFloat32(this.position, true);
        this.position += 4;
        return v;
    }

    readString(length) {
        if (this.position + length > this.length) {
            // Clamp to available data
            length = Math.max(0, this.length - this.position);
        }
        let v = '';
        const end = this.position + length;

        while (this.position < end && this.position < this.length) {
            const val = this.data.getUint8(this.position++);
            if (val === 0) break;
            v += String.fromCharCode(val);
        }

        this.position = end;
        return v.trim();
    }

    readBytes(length) {
        if (this.position + length > this.length) {
            // Clamp to available data
            length = Math.max(0, this.length - this.position);
        }
        const bytes = new Uint8Array(this.data.buffer, this.position, length);
        this.position += length;
        return bytes;
    }

    readTextureDictionary(dictHeader) {
        const result = {
            textures: [],
            version: dictHeader.version,
            build: dictHeader.build
        };

        const structHeader = this.readHeader();
        if (structHeader.type !== ChunkType.CHUNK_STRUCT) {
            throw new Error('Expected struct chunk');
        }

        const textureCount = this.readUInt16();
        const deviceId = this.readUInt16();
        result.deviceId = deviceId;

        for (let i = 0; i < textureCount; i++) {
            try {
                const texture = this.readTextureNative();
                if (texture) {
                    result.textures.push(texture);
                }
            } catch (e) {
                break;
            }
        }

        const remaining = dictHeader.length - (this.position - 12);
        if (remaining > 0) {
            const extHeader = this.readHeader();
            if (extHeader.type === ChunkType.CHUNK_EXTENSION) {
                this.position += extHeader.length;
            }
        }

        return result;
    }

    readTextureNative() {
        const header = this.readHeader();

        if (header.type !== ChunkType.CHUNK_TEXTURENATIVE) {
            this.position += header.length;
            return null;
        }

        const chunkEndPos = this.position + header.length;
        const texture = {};

        this.readHeader();

        const platformId = this.readUInt32();
        texture.platform = platformId;
        texture.filterFlags = this.readUInt32();
        texture.nameOffset = this.position;  // Track offset for renaming
        texture.name = this.readString(32);
        texture.maskNameOffset = this.position;  // Track offset for renaming
        texture.maskName = this.readString(32);
        texture.rasterFormat = this.readUInt32();

        if (platformId === PlatformID.PLATFORM_D3D8 || platformId === PlatformID.PLATFORM_D3D9) {
            const formatField = this.readUInt32();
            texture.d3dFormatRaw = formatField;
            texture.d3dFormat = formatField;
            texture.width = this.readUInt16();
            texture.height = this.readUInt16();
            texture.depth = this.readUInt8();
            texture.mipmapCount = this.readUInt8();
            texture.rasterType = this.readUInt8();

            if (platformId === PlatformID.PLATFORM_D3D9) {
                texture.alpha = this.readUInt8();
                // D3D9: alpha byte indicates if texture has alpha channel
                texture.hasAlpha = texture.alpha > 0;
            } else {
                const dxtType = this.readUInt8();
                texture.alpha = dxtType;
                texture.dxtType = dxtType;
                texture.hasAlpha = formatField !== 0;
                texture.isCompressed = dxtType > 0;

                if (dxtType === 1) {
                    texture.d3dFormat = D3DFormat.D3DFMT_DXT1;
                } else if (dxtType === 3) {
                    texture.d3dFormat = D3DFormat.D3DFMT_DXT3;
                } else if (dxtType === 5) {
                    texture.d3dFormat = D3DFormat.D3DFMT_DXT5;
                }
            }

            texture.imageData = this.readTextureData(texture);

            // Store human-readable format for UI
            texture.originalFormat = this.getFormatName(texture.d3dFormat);
        } else {
            // PS2 or unknown platform
            this.position = chunkEndPos;
            texture.imageData = null;
            return texture;
        }

        if (this.position < chunkEndPos) {
            try {
                const extHeader = this.readHeader();
                if (extHeader.type === ChunkType.CHUNK_EXTENSION) {
                    this.position += extHeader.length;
                }
            } catch (e) {
                // Extension might not be present
            }
        }

        if (this.position !== chunkEndPos) {
            this.position = chunkEndPos;
        }

        return texture;
    }

    readTextureData(texture) {
        const isPaletted = (texture.rasterFormat & RasterFormat.FORMAT_EXT_PAL8) !== 0 ||
                          (texture.rasterFormat & RasterFormat.FORMAT_EXT_PAL4) !== 0;

        let palette = null;

        if (isPaletted) {
            const paletteSize = (texture.rasterFormat & RasterFormat.FORMAT_EXT_PAL8) ? 256 : 16;
            palette = new Uint8Array(paletteSize * 4);
            for (let i = 0; i < paletteSize; i++) {
                palette[i * 4 + 2] = this.readUInt8(); // B
                palette[i * 4 + 1] = this.readUInt8(); // G
                palette[i * 4 + 0] = this.readUInt8(); // R
                palette[i * 4 + 3] = this.readUInt8(); // A
            }
            // Store palette for preservation
            texture.palette = palette;
            texture.isPaletted = true;
        } else {
            texture.isPaletted = false;
        }

        const dataSize = this.readUInt32();

        if (dataSize > this.length - this.position || dataSize > 100 * 1024 * 1024) {
            return null;
        }

        const rawData = this.readBytes(dataSize);

        // Store raw compressed data for pass-through preservation
        // Make a copy since readBytes returns a view into the buffer
        texture.rawData = new Uint8Array(rawData);
        texture.rawDataSize = dataSize;

        // Read and store all mipmap levels for preservation
        texture.mipmaps = [];
        if (texture.mipmapCount > 1) {
            for (let mip = 1; mip < texture.mipmapCount; mip++) {
                if (this.position + 4 > this.length) break;
                const mipSize = this.readUInt32();
                if (mipSize > 0 && this.position + mipSize <= this.length) {
                    const mipData = this.readBytes(mipSize);
                    // Make a copy of mipmap data
                    texture.mipmaps.push({
                        size: mipSize,
                        data: new Uint8Array(mipData)
                    });
                }
            }
        }

        // Decode for preview/display purposes
        const imageData = this.decodeTexture(texture, rawData, palette);

        // If hasAlpha wasn't detected from header flags, scan the decoded image data
        // This catches DXT1 textures that actually use transparent pixels (3-color mode)
        if (!texture.hasAlpha && imageData) {
            texture.hasAlpha = this.detectAlphaFromImageData(imageData);
        }

        return imageData;
    }

    /**
     * Scan decoded RGBA image data to detect if any pixel has alpha < 255
     * This is a fallback for when header flags don't indicate alpha
     * @param {Uint8Array} imageData - RGBA pixel data
     * @returns {boolean} - true if any pixel has alpha < 255
     */
    detectAlphaFromImageData(imageData) {
        // Sample pixels for performance - check every 4th pixel
        // For a 256x256 texture this checks ~16k pixels which is fast enough
        for (let i = 3; i < imageData.length; i += 16) {
            if (imageData[i] < 255) {
                return true;
            }
        }
        // If no alpha found in sampling, do a more thorough check on smaller portion
        // Check first 1000 pixels fully
        const checkCount = Math.min(imageData.length / 4, 1000);
        for (let i = 0; i < checkCount; i++) {
            if (imageData[i * 4 + 3] < 255) {
                return true;
            }
        }
        return false;
    }

    decodeTexture(texture, rawData, palette) {
        const width = texture.width;
        const height = texture.height;
        const rgba = new Uint8Array(width * height * 4);
        const d3dFmt = texture.d3dFormat;
        const pixelCount = width * height;

        if (d3dFmt === D3DFormat.D3DFMT_DXT1) {
            return this.decodeDXT1(rawData, width, height);
        } else if (d3dFmt === D3DFormat.D3DFMT_DXT3) {
            return this.decodeDXT3(rawData, width, height);
        } else if (d3dFmt === D3DFormat.D3DFMT_DXT5) {
            return this.decodeDXT5(rawData, width, height);
        }

        if (palette) {
            for (let i = 0; i < width * height; i++) {
                const index = rawData[i];
                rgba[i * 4 + 0] = palette[index * 4 + 0];
                rgba[i * 4 + 1] = palette[index * 4 + 1];
                rgba[i * 4 + 2] = palette[index * 4 + 2];
                rgba[i * 4 + 3] = palette[index * 4 + 3];
            }
            return rgba;
        }

        switch (d3dFmt) {
            case D3DFormat.D3DFMT_A8R8G8B8:
            case D3DFormat.D3DFMT_X8R8G8B8:
                for (let i = 0; i < width * height; i++) {
                    rgba[i * 4 + 0] = rawData[i * 4 + 2];
                    rgba[i * 4 + 1] = rawData[i * 4 + 1];
                    rgba[i * 4 + 2] = rawData[i * 4 + 0];
                    rgba[i * 4 + 3] = d3dFmt === D3DFormat.D3DFMT_A8R8G8B8 ? rawData[i * 4 + 3] : 255;
                }
                return rgba;

            case D3DFormat.D3DFMT_R5G6B5:
                for (let i = 0; i < width * height; i++) {
                    const pixel = rawData[i * 2] | (rawData[i * 2 + 1] << 8);
                    rgba[i * 4 + 0] = ((pixel >> 11) & 0x1F) * 255 / 31;
                    rgba[i * 4 + 1] = ((pixel >> 5) & 0x3F) * 255 / 63;
                    rgba[i * 4 + 2] = (pixel & 0x1F) * 255 / 31;
                    rgba[i * 4 + 3] = 255;
                }
                return rgba;

            case D3DFormat.D3DFMT_A1R5G5B5:
            case D3DFormat.D3DFMT_X1R5G5B5:
                for (let i = 0; i < width * height; i++) {
                    const pixel = rawData[i * 2] | (rawData[i * 2 + 1] << 8);
                    rgba[i * 4 + 0] = ((pixel >> 10) & 0x1F) * 255 / 31;
                    rgba[i * 4 + 1] = ((pixel >> 5) & 0x1F) * 255 / 31;
                    rgba[i * 4 + 2] = (pixel & 0x1F) * 255 / 31;
                    rgba[i * 4 + 3] = d3dFmt === D3DFormat.D3DFMT_A1R5G5B5 ? ((pixel >> 15) ? 255 : 0) : 255;
                }
                return rgba;

            case D3DFormat.D3DFMT_A4R4G4B4:
                for (let i = 0; i < width * height; i++) {
                    const pixel = rawData[i * 2] | (rawData[i * 2 + 1] << 8);
                    rgba[i * 4 + 0] = ((pixel >> 8) & 0xF) * 17;
                    rgba[i * 4 + 1] = ((pixel >> 4) & 0xF) * 17;
                    rgba[i * 4 + 2] = (pixel & 0xF) * 17;
                    rgba[i * 4 + 3] = ((pixel >> 12) & 0xF) * 17;
                }
                return rgba;

            case D3DFormat.D3DFMT_P8:
                return rgba;
        }

        // Fallback by rasterFormat
        const format = texture.rasterFormat & RasterFormat.FORMAT_MASK;

        // Some GTA III / Vice City D3D8 TXDs store 8-bit alpha masks with a
        // 4444 raster flag and one byte per pixel. Render them as white alpha
        // masks instead of misreading pairs of bytes as 16-bit color.
        if (format === RasterFormat.FORMAT_4444 && rawData.length === pixelCount) {
            for (let i = 0; i < pixelCount; i++) {
                rgba[i * 4 + 0] = 255;
                rgba[i * 4 + 1] = 255;
                rgba[i * 4 + 2] = 255;
                rgba[i * 4 + 3] = rawData[i];
            }
            return rgba;
        }

        if (format === RasterFormat.FORMAT_LUM8 && rawData.length === pixelCount) {
            for (let i = 0; i < pixelCount; i++) {
                rgba[i * 4 + 0] = rawData[i];
                rgba[i * 4 + 1] = rawData[i];
                rgba[i * 4 + 2] = rawData[i];
                rgba[i * 4 + 3] = 255;
            }
            return rgba;
        }

        switch (format) {
            case RasterFormat.FORMAT_8888:
            case 0:
                for (let i = 0; i < width * height; i++) {
                    rgba[i * 4 + 0] = rawData[i * 4 + 2];
                    rgba[i * 4 + 1] = rawData[i * 4 + 1];
                    rgba[i * 4 + 2] = rawData[i * 4 + 0];
                    rgba[i * 4 + 3] = rawData[i * 4 + 3];
                }
                break;

            case RasterFormat.FORMAT_888:
                for (let i = 0; i < width * height; i++) {
                    rgba[i * 4 + 0] = rawData[i * 3 + 2];
                    rgba[i * 4 + 1] = rawData[i * 3 + 1];
                    rgba[i * 4 + 2] = rawData[i * 3 + 0];
                    rgba[i * 4 + 3] = 255;
                }
                break;

            case RasterFormat.FORMAT_565:
                for (let i = 0; i < width * height; i++) {
                    const pixel = rawData[i * 2] | (rawData[i * 2 + 1] << 8);
                    rgba[i * 4 + 0] = ((pixel >> 11) & 0x1F) << 3;
                    rgba[i * 4 + 1] = ((pixel >> 5) & 0x3F) << 2;
                    rgba[i * 4 + 2] = (pixel & 0x1F) << 3;
                    rgba[i * 4 + 3] = 255;
                }
                break;

            case RasterFormat.FORMAT_1555:
                for (let i = 0; i < width * height; i++) {
                    const pixel = rawData[i * 2] | (rawData[i * 2 + 1] << 8);
                    rgba[i * 4 + 0] = ((pixel >> 10) & 0x1F) << 3;
                    rgba[i * 4 + 1] = ((pixel >> 5) & 0x1F) << 3;
                    rgba[i * 4 + 2] = (pixel & 0x1F) << 3;
                    rgba[i * 4 + 3] = (pixel >> 15) ? 255 : 0;
                }
                break;

            case RasterFormat.FORMAT_4444:
                for (let i = 0; i < width * height; i++) {
                    const pixel = rawData[i * 2] | (rawData[i * 2 + 1] << 8);
                    rgba[i * 4 + 0] = ((pixel >> 8) & 0xF) << 4;
                    rgba[i * 4 + 1] = ((pixel >> 4) & 0xF) << 4;
                    rgba[i * 4 + 2] = (pixel & 0xF) << 4;
                    rgba[i * 4 + 3] = ((pixel >> 12) & 0xF) << 4;
                }
                break;

            default:
                if (rawData.length >= width * height * 4) {
                    for (let i = 0; i < width * height; i++) {
                        rgba[i * 4 + 0] = rawData[i * 4 + 2];
                        rgba[i * 4 + 1] = rawData[i * 4 + 1];
                        rgba[i * 4 + 2] = rawData[i * 4 + 0];
                        rgba[i * 4 + 3] = rawData[i * 4 + 3];
                    }
                } else {
                    rgba.fill(128);
                }
        }

        return rgba;
    }

    decodeDXT1(data, width, height) {
        const output = new Uint8Array(width * height * 4);
        const blocksX = Math.max(1, Math.floor((width + 3) / 4));
        const blocksY = Math.max(1, Math.floor((height + 3) / 4));

        let dataOffset = 0;

        for (let by = 0; by < blocksY; by++) {
            for (let bx = 0; bx < blocksX; bx++) {
                const c0 = data[dataOffset] | (data[dataOffset + 1] << 8);
                const c1 = data[dataOffset + 2] | (data[dataOffset + 3] << 8);
                dataOffset += 4;

                const colors = this.dxt1Colors(c0, c1);
                const indices = data[dataOffset] | (data[dataOffset + 1] << 8) |
                               (data[dataOffset + 2] << 16) | (data[dataOffset + 3] << 24);
                dataOffset += 4;

                for (let y = 0; y < 4; y++) {
                    for (let x = 0; x < 4; x++) {
                        const px = bx * 4 + x;
                        const py = by * 4 + y;
                        if (px >= width || py >= height) continue;

                        const idx = (indices >> ((y * 4 + x) * 2)) & 0x3;
                        const color = colors[idx];
                        const outIdx = (py * width + px) * 4;

                        output[outIdx + 0] = color[0];
                        output[outIdx + 1] = color[1];
                        output[outIdx + 2] = color[2];
                        output[outIdx + 3] = color[3];
                    }
                }
            }
        }

        return output;
    }

    dxt1Colors(c0, c1, force4Color = false) {
        const colors = [];

        // RGB565: bits 11-15=R (5 bits), 5-10=G (6 bits), 0-4=B (5 bits)
        const r0 = ((c0 >> 11) & 0x1F);
        const g0 = ((c0 >> 5) & 0x3F);
        const b0 = (c0 & 0x1F);

        const r1 = ((c1 >> 11) & 0x1F);
        const g1 = ((c1 >> 5) & 0x3F);
        const b1 = (c1 & 0x1F);

        // Scale to 8-bit: R and B (5 bit) multiply by 8.23 (~255/31), G (6 bit) by 4.05 (~255/63)
        colors[0] = [
            (r0 << 3) | (r0 >> 2),  // R: scale 5-bit to 8-bit properly
            (g0 << 2) | (g0 >> 4),  // G: scale 6-bit to 8-bit properly
            (b0 << 3) | (b0 >> 2),  // B: scale 5-bit to 8-bit properly
            255
        ];

        colors[1] = [
            (r1 << 3) | (r1 >> 2),  // R
            (g1 << 2) | (g1 >> 4),  // G
            (b1 << 3) | (b1 >> 2),  // B
            255
        ];

        // DXT3/DXT5 always use 4-color mode (force4Color=true)
        // DXT1 uses 4-color when c0 > c1, otherwise 3-color + transparent
        if (c0 > c1 || force4Color) {
            colors[2] = [
                Math.floor((2 * colors[0][0] + colors[1][0]) / 3),
                Math.floor((2 * colors[0][1] + colors[1][1]) / 3),
                Math.floor((2 * colors[0][2] + colors[1][2]) / 3),
                255
            ];
            colors[3] = [
                Math.floor((colors[0][0] + 2 * colors[1][0]) / 3),
                Math.floor((colors[0][1] + 2 * colors[1][1]) / 3),
                Math.floor((colors[0][2] + 2 * colors[1][2]) / 3),
                255
            ];
        } else {
            colors[2] = [
                Math.floor((colors[0][0] + colors[1][0]) / 2),
                Math.floor((colors[0][1] + colors[1][1]) / 2),
                Math.floor((colors[0][2] + colors[1][2]) / 2),
                255
            ];
            colors[3] = [0, 0, 0, 0];
        }

        return colors;
    }

    decodeDXT3(data, width, height) {
        const output = new Uint8Array(width * height * 4);
        const blocksX = Math.max(1, Math.floor((width + 3) / 4));
        const blocksY = Math.max(1, Math.floor((height + 3) / 4));

        let dataOffset = 0;

        for (let by = 0; by < blocksY; by++) {
            for (let bx = 0; bx < blocksX; bx++) {
                const alphaBlock = [];
                for (let i = 0; i < 8; i++) {
                    alphaBlock.push(data[dataOffset++]);
                }

                const c0 = data[dataOffset] | (data[dataOffset + 1] << 8);
                const c1 = data[dataOffset + 2] | (data[dataOffset + 3] << 8);
                dataOffset += 4;

                const colors = this.dxt1Colors(c0, c1, true);

                const indices = data[dataOffset] | (data[dataOffset + 1] << 8) |
                               (data[dataOffset + 2] << 16) | (data[dataOffset + 3] << 24);
                dataOffset += 4;

                for (let y = 0; y < 4; y++) {
                    for (let x = 0; x < 4; x++) {
                        const px = bx * 4 + x;
                        const py = by * 4 + y;
                        if (px >= width || py >= height) continue;

                        const idx = (indices >> ((y * 4 + x) * 2)) & 0x3;
                        const color = colors[idx];
                        const outIdx = (py * width + px) * 4;

                        const alphaIdx = y * 4 + x;
                        const alphaByte = alphaBlock[Math.floor(alphaIdx / 2)];
                        const alpha = ((alphaIdx % 2 === 0) ? (alphaByte & 0xF) : (alphaByte >> 4)) * 17;

                        output[outIdx + 0] = color[0];
                        output[outIdx + 1] = color[1];
                        output[outIdx + 2] = color[2];
                        output[outIdx + 3] = alpha;
                    }
                }
            }
        }

        return output;
    }

    decodeDXT5(data, width, height) {
        const output = new Uint8Array(width * height * 4);
        const blocksX = Math.max(1, Math.floor((width + 3) / 4));
        const blocksY = Math.max(1, Math.floor((height + 3) / 4));

        let dataOffset = 0;

        for (let by = 0; by < blocksY; by++) {
            for (let bx = 0; bx < blocksX; bx++) {
                const alpha0 = data[dataOffset++];
                const alpha1 = data[dataOffset++];

                const alphaIndices = [];
                for (let i = 0; i < 6; i++) {
                    alphaIndices.push(data[dataOffset++]);
                }

                const alphas = this.dxt5Alphas(alpha0, alpha1);

                const c0 = data[dataOffset] | (data[dataOffset + 1] << 8);
                const c1 = data[dataOffset + 2] | (data[dataOffset + 3] << 8);
                dataOffset += 4;

                const colors = this.dxt1Colors(c0, c1, true);

                const indices = data[dataOffset] | (data[dataOffset + 1] << 8) |
                               (data[dataOffset + 2] << 16) | (data[dataOffset + 3] << 24);
                dataOffset += 4;

                for (let y = 0; y < 4; y++) {
                    for (let x = 0; x < 4; x++) {
                        const px = bx * 4 + x;
                        const py = by * 4 + y;
                        if (px >= width || py >= height) continue;

                        const idx = (indices >> ((y * 4 + x) * 2)) & 0x3;
                        const color = colors[idx];
                        const outIdx = (py * width + px) * 4;

                        const alphaPixelIdx = y * 4 + x;
                        const alphaByteIdx = Math.floor(alphaPixelIdx * 3 / 8);
                        const alphaBitIdx = (alphaPixelIdx * 3) % 8;

                        let alphaIdx;
                        if (alphaBitIdx <= 5) {
                            alphaIdx = (alphaIndices[alphaByteIdx] >> alphaBitIdx) & 0x7;
                        } else {
                            alphaIdx = ((alphaIndices[alphaByteIdx] >> alphaBitIdx) |
                                       (alphaIndices[alphaByteIdx + 1] << (8 - alphaBitIdx))) & 0x7;
                        }

                        output[outIdx + 0] = color[0];
                        output[outIdx + 1] = color[1];
                        output[outIdx + 2] = color[2];
                        output[outIdx + 3] = alphas[alphaIdx];
                    }
                }
            }
        }

        return output;
    }

    dxt5Alphas(a0, a1) {
        const alphas = [a0, a1];

        if (a0 > a1) {
            for (let i = 1; i <= 6; i++) {
                alphas.push(Math.floor(((7 - i) * a0 + i * a1) / 7));
            }
        } else {
            for (let i = 1; i <= 4; i++) {
                alphas.push(Math.floor(((5 - i) * a0 + i * a1) / 5));
            }
            alphas.push(0);
            alphas.push(255);
        }

        return alphas;
    }

    /**
     * Convert D3D format code to human-readable format name
     */
    getFormatName(d3dFormat) {
        switch (d3dFormat) {
            case D3DFormat.D3DFMT_DXT1:
                return 'dxt1';
            case D3DFormat.D3DFMT_DXT3:
                return 'dxt3';
            case D3DFormat.D3DFMT_DXT5:
                return 'dxt5';
            case D3DFormat.D3DFMT_A8R8G8B8:
            case D3DFormat.D3DFMT_X8R8G8B8:
                return 'rgba';
            case D3DFormat.D3DFMT_R5G6B5:
            case D3DFormat.D3DFMT_A1R5G5B5:
            case D3DFormat.D3DFMT_X1R5G5B5:
            case D3DFormat.D3DFMT_A4R4G4B4:
                return 'rgb16';
            case D3DFormat.D3DFMT_P8:
                return 'pal8';
            default:
                return 'rgba'; // Default fallback
        }
    }
}
