#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <Windows.h>
    #include <Unknwn.h>
    #include <objbase.h>
    #include <dxcapi.h>
    #include <d3d12shader.h>
#else
    #include <cstdlib>
    #include <dlfcn.h>

    #if defined(MORBI_HAS_SYSTEM_DXCAPI_H)
        #include <dxc/dxcapi.h>
    #else
        // Cross-platform ABI-compatible COM & DXC declarations for Linux/POSIX when WinAdapter.h is unavailable.
        #ifndef STDMETHODCALLTYPE
            #define STDMETHODCALLTYPE
        #endif
        #ifndef __stdcall
            #define __stdcall
        #endif

        using HRESULT = int32_t;
        using BOOL = int32_t;
        using UINT64 = uint64_t;
        using INT = int32_t;
        using UINT = uint32_t;
        using UINT32 = uint32_t;
        using SIZE_T = size_t;
        using BYTE = uint8_t;
        using LPCVOID = const void*;
        using LPVOID = void*;
        using LPCSTR = const char*;
        using LPCWSTR = const wchar_t*;
        using WCHAR = wchar_t;

        #ifndef S_OK
            #define S_OK static_cast<HRESULT>(0)
        #endif
        #ifndef E_NOINTERFACE
            #define E_NOINTERFACE static_cast<HRESULT>(0x80004002L)
        #endif
        #ifndef E_FAIL
            #define E_FAIL static_cast<HRESULT>(0x80004005L)
        #endif
        #ifndef E_POINTER
            #define E_POINTER static_cast<HRESULT>(0x80004003L)
        #endif
        #ifndef SUCCEEDED
            #define SUCCEEDED(hr) (static_cast<HRESULT>(hr) >= 0)
        #endif
        #ifndef FAILED
            #define FAILED(hr) (static_cast<HRESULT>(hr) < 0)
        #endif
        #ifndef TRUE
            #define TRUE 1
        #endif
        #ifndef FALSE
            #define FALSE 0
        #endif

        struct GUID
        {
            uint32_t Data1;
            uint16_t Data2;
            uint16_t Data3;
            uint8_t  Data4[8];
        };
        using IID = GUID;
        using CLSID = GUID;
        using REFIID = const IID&;
        using REFCLSID = const CLSID&;

        inline bool operator==(const GUID& a, const GUID& b) noexcept
        {
            return std::memcmp(&a, &b, sizeof(GUID)) == 0;
        }

        struct IUnknown
        {
            virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) = 0;
            virtual uint32_t STDMETHODCALLTYPE AddRef() = 0;
            virtual uint32_t STDMETHODCALLTYPE Release() = 0;
            virtual ~IUnknown() = default;
        };

        struct IDxcBlob : public IUnknown
        {
            virtual LPVOID STDMETHODCALLTYPE GetBufferPointer() = 0;
            virtual SIZE_T STDMETHODCALLTYPE GetBufferSize() = 0;
        };

        struct IDxcBlobEncoding : public IDxcBlob
        {
            virtual HRESULT STDMETHODCALLTYPE GetEncoding(BOOL* pKnown, UINT32* pCodePage) = 0;
        };

        struct IDxcBlobWide : public IDxcBlobEncoding
        {
            virtual LPCWSTR STDMETHODCALLTYPE GetStringPointer() = 0;
            virtual SIZE_T STDMETHODCALLTYPE GetStringLength() = 0;
        };

        struct IDxcBlobUtf8 : public IDxcBlobEncoding
        {
            virtual LPCSTR STDMETHODCALLTYPE GetStringPointer() = 0;
            virtual SIZE_T STDMETHODCALLTYPE GetStringLength() = 0;
        };

        struct IDxcIncludeHandler : public IUnknown
        {
            virtual HRESULT STDMETHODCALLTYPE LoadSource(LPCWSTR pFilename, IDxcBlob** ppIncludeSource) = 0;
        };

        struct DxcBuffer
        {
            LPCVOID Ptr;
            SIZE_T Size;
            UINT Encoding;
        };

        struct IDxcOperationResult : public IUnknown
        {
            virtual HRESULT STDMETHODCALLTYPE GetStatus(HRESULT* pStatus) = 0;
            virtual HRESULT STDMETHODCALLTYPE GetResult(IDxcBlob** ppResult) = 0;
            virtual HRESULT STDMETHODCALLTYPE GetErrorBuffer(IDxcBlobEncoding** ppErrors) = 0;
        };

        enum DXC_OUT_KIND
        {
            DXC_OUT_NONE = 0,
            DXC_OUT_OBJECT = 1,
            DXC_OUT_ERRORS = 2,
            DXC_OUT_PDB = 3,
            DXC_OUT_SHADER_HASH = 4,
            DXC_OUT_DISASSEMBLY = 5,
            DXC_OUT_HLSL = 6,
            DXC_OUT_TEXT = 7,
            DXC_OUT_REFLECTION = 8,
            DXC_OUT_ROOT_SIGNATURE = 9,
            DXC_OUT_EXTRA_OUTPUTS = 10,
            DXC_OUT_REMARKS = 11,
            DXC_OUT_TIME_REPORT = 12,
            DXC_OUT_TIME_TRACE = 13
        };

        struct IDxcResult : public IDxcOperationResult
        {
            virtual BOOL STDMETHODCALLTYPE HasOutput(DXC_OUT_KIND dxcOutKind) = 0;
            virtual HRESULT STDMETHODCALLTYPE GetOutput(
                DXC_OUT_KIND dxcOutKind,
                REFIID iid,
                void** ppvObject,
                IDxcBlobWide** ppOutputName) = 0;
            virtual UINT32 GetNumOutputs() = 0;
            virtual DXC_OUT_KIND GetOutputByIndex(UINT32 Index) = 0;
            virtual DXC_OUT_KIND PrimaryOutput() = 0;
        };

        struct IDxcCompilerArgs : public IUnknown {};
        struct IStream;
        struct IMalloc;

        struct IDxcUtils : public IUnknown
        {
            virtual HRESULT STDMETHODCALLTYPE CreateBlobFromBlob(IDxcBlob* pBlob, UINT32 offset, UINT32 length, IDxcBlob** ppResult) = 0;
            virtual HRESULT STDMETHODCALLTYPE CreateBlobFromPinned(LPCVOID pData, UINT32 size, UINT32 codePage, IDxcBlobEncoding** ppBlobEncoding) = 0;
            virtual HRESULT STDMETHODCALLTYPE MoveToBlob(LPCVOID pData, IMalloc* pIMalloc, UINT32 size, UINT32 codePage, IDxcBlobEncoding** ppBlobEncoding) = 0;
            virtual HRESULT STDMETHODCALLTYPE CreateBlob(LPCVOID pData, UINT32 size, UINT32 codePage, IDxcBlobEncoding** ppBlobEncoding) = 0;
            virtual HRESULT STDMETHODCALLTYPE LoadFile(LPCWSTR pFileName, UINT32* pCodePage, IDxcBlobEncoding** ppBlobEncoding) = 0;
            virtual HRESULT STDMETHODCALLTYPE CreateReadOnlyStreamFromBlob(IDxcBlob* pBlob, IStream** ppStream) = 0;
            virtual HRESULT STDMETHODCALLTYPE CreateDefaultIncludeHandler(IDxcIncludeHandler** ppResult) = 0;
            virtual HRESULT STDMETHODCALLTYPE GetBlobAsUtf8(IDxcBlob* pBlob, IDxcBlobUtf8** ppBlobEncoding) = 0;
            virtual HRESULT STDMETHODCALLTYPE GetBlobAsWide(IDxcBlob* pBlob, IDxcBlobWide** ppBlobEncoding) = 0;
        };

        struct IDxcCompiler3 : public IUnknown
        {
            virtual HRESULT STDMETHODCALLTYPE Compile(
                const DxcBuffer* pSource,
                LPCWSTR* pArguments,
                UINT32 argCount,
                IDxcIncludeHandler* pIncludeHandler,
                REFIID riid,
                LPVOID* ppResult) = 0;
            virtual HRESULT STDMETHODCALLTYPE Disassemble(
                const DxcBuffer* pObject,
                REFIID riid,
                LPVOID* ppResult) = 0;
        };

        struct IDxcVersionInfo : public IUnknown
        {
            virtual HRESULT STDMETHODCALLTYPE GetVersion(UINT32* pMajor, UINT32* pMinor) = 0;
            virtual HRESULT STDMETHODCALLTYPE GetFlags(UINT32* pFlags) = 0;
        };

        struct IDxcVersionInfo2 : public IDxcVersionInfo
        {
            virtual HRESULT STDMETHODCALLTYPE GetCommitInfo(UINT32* pCommitCount, char** pCommitHash) = 0;
        };

        struct IDxcVersionInfo3 : public IUnknown
        {
            virtual HRESULT STDMETHODCALLTYPE GetCustomVersionString(char** pVersionString) = 0;
        };

        struct IDxcContainerReflection : public IUnknown
        {
            virtual HRESULT STDMETHODCALLTYPE Load(IDxcBlob* pContainer) = 0;
            virtual HRESULT STDMETHODCALLTYPE GetPartCount(UINT32* pResult) = 0;
            virtual HRESULT STDMETHODCALLTYPE GetPartKind(UINT32 idx, UINT32* pResult) = 0;
            virtual HRESULT STDMETHODCALLTYPE GetPartContent(UINT32 idx, IDxcBlob** ppResult) = 0;
            virtual HRESULT STDMETHODCALLTYPE FindFirstPartKind(UINT32 kind, UINT32* pResult) = 0;
            virtual HRESULT STDMETHODCALLTYPE GetPartReflection(UINT32 idx, REFIID iid, void** ppvObject) = 0;
        };

        #define DXC_FOURCC(ch0, ch1, ch2, ch3) \
            (static_cast<uint32_t>(static_cast<uint8_t>(ch0)) | \
            (static_cast<uint32_t>(static_cast<uint8_t>(ch1)) << 8) | \
            (static_cast<uint32_t>(static_cast<uint8_t>(ch2)) << 16) | \
            (static_cast<uint32_t>(static_cast<uint8_t>(ch3)) << 24))
        #define DXC_PART_DXIL DXC_FOURCC('D', 'X', 'I', 'L')
        #define DXC_PART_REFLECTION_DATA DXC_FOURCC('S', 'T', 'A', 'T')

        enum D3D_SRV_DIMENSION
        {
            D3D_SRV_DIMENSION_UNKNOWN = 0,
            D3D_SRV_DIMENSION_BUFFER = 1,
            D3D_SRV_DIMENSION_TEXTURE1D = 2,
            D3D_SRV_DIMENSION_TEXTURE1DARRAY = 3,
            D3D_SRV_DIMENSION_TEXTURE2D = 4,
            D3D_SRV_DIMENSION_TEXTURE2DARRAY = 5,
            D3D_SRV_DIMENSION_TEXTURE2DMS = 6,
            D3D_SRV_DIMENSION_TEXTURE2DMSARRAY = 7,
            D3D_SRV_DIMENSION_TEXTURE3D = 8,
            D3D_SRV_DIMENSION_TEXTURECUBE = 9,
            D3D_SRV_DIMENSION_TEXTURECUBEARRAY = 10,
            D3D_SRV_DIMENSION_BUFFEREX = 11
        };

        enum D3D_SHADER_VARIABLE_CLASS
        {
            D3D_SVC_SCALAR = 0,
            D3D_SVC_VECTOR = 1,
            D3D_SVC_MATRIX_ROWS = 2,
            D3D_SVC_MATRIX_COLUMNS = 3,
            D3D_SVC_OBJECT = 4,
            D3D_SVC_STRUCT = 5
        };

        enum D3D_SHADER_VARIABLE_TYPE
        {
            D3D_SVT_VOID = 0,
            D3D_SVT_BOOL = 1,
            D3D_SVT_INT = 2,
            D3D_SVT_FLOAT = 3,
            D3D_SVT_UINT = 19,
            D3D_SVT_DOUBLE = 39,
            D3D_SVT_MIN8FLOAT = 52,
            D3D_SVT_MIN10FLOAT = 53,
            D3D_SVT_MIN16FLOAT = 54,
            D3D_SVT_MIN12INT = 55,
            D3D_SVT_MIN16INT = 56,
            D3D_SVT_MIN16UINT = 57,
            D3D_SVT_INT16 = 58,
            D3D_SVT_UINT16 = 59,
            D3D_SVT_FLOAT16 = 60,
            D3D_SVT_INT64 = 61,
            D3D_SVT_UINT64 = 62
        };

        enum D3D_SHADER_INPUT_FLAGS
        {
            D3D_SIF_USERPACKED = 0x1,
            D3D_SIF_COMPARISON_SAMPLER = 0x2,
            D3D_SIF_UNUSED = 0x10
        };

        enum D3D_SHADER_INPUT_TYPE
        {
            D3D_SIT_CBUFFER = 0,
            D3D_SIT_TBUFFER = 1,
            D3D_SIT_TEXTURE = 2,
            D3D_SIT_SAMPLER = 3,
            D3D_SIT_UAV_RWTYPED = 4,
            D3D_SIT_STRUCTURED = 5,
            D3D_SIT_UAV_RWSTRUCTURED = 6,
            D3D_SIT_BYTEADDRESS = 7,
            D3D_SIT_UAV_RWBYTEADDRESS = 8,
            D3D_SIT_UAV_APPEND_STRUCTURED = 9,
            D3D_SIT_UAV_CONSUME_STRUCTURED = 10,
            D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER = 11,
            D3D_SIT_RTACCELERATIONSTRUCTURE = 12,
            D3D_SIT_UAV_FEEDBACKTEXTURE = 13
        };

        enum D3D_CBUFFER_TYPE
        {
            D3D_CT_CBUFFER = 0,
            D3D_CT_TBUFFER = 1,
            D3D_CT_INTERFACE_POINTERS = 2,
            D3D_CT_RESOURCE_BIND_INFO = 3
        };

        enum D3D_NAME
        {
            D3D_NAME_UNDEFINED = 0,
            D3D_NAME_POSITION = 1,
            D3D_NAME_CLIP_DISTANCE = 2,
            D3D_NAME_CULL_DISTANCE = 3,
            D3D_NAME_RENDER_TARGET_ARRAY_INDEX = 4,
            D3D_NAME_VIEWPORT_ARRAY_INDEX = 5,
            D3D_NAME_VERTEX_ID = 6,
            D3D_NAME_PRIMITIVE_ID = 7,
            D3D_NAME_INSTANCE_ID = 8,
            D3D_NAME_IS_FRONT_FACE = 9,
            D3D_NAME_SAMPLE_INDEX = 10,
            D3D_NAME_TARGET = 64,
            D3D_NAME_DEPTH = 65,
            D3D_NAME_COVERAGE = 66
        };

        enum D3D_REGISTER_COMPONENT_TYPE
        {
            D3D_REGISTER_COMPONENT_UNKNOWN = 0,
            D3D_REGISTER_COMPONENT_UINT32 = 1,
            D3D_REGISTER_COMPONENT_SINT32 = 2,
            D3D_REGISTER_COMPONENT_FLOAT32 = 3,
            D3D_REGISTER_COMPONENT_UINT16 = 4,
            D3D_REGISTER_COMPONENT_SINT16 = 5,
            D3D_REGISTER_COMPONENT_FLOAT16 = 6,
            D3D_REGISTER_COMPONENT_UINT64 = 7,
            D3D_REGISTER_COMPONENT_SINT64 = 8,
            D3D_REGISTER_COMPONENT_FLOAT64 = 9
        };

        enum D3D12_SHADER_VERSION_TYPE
        {
            D3D12_SHVER_PIXEL_SHADER = 0,
            D3D12_SHVER_VERTEX_SHADER = 1,
            D3D12_SHVER_GEOMETRY_SHADER = 2,
            D3D12_SHVER_HULL_SHADER = 3,
            D3D12_SHVER_DOMAIN_SHADER = 4,
            D3D12_SHVER_COMPUTE_SHADER = 5,
            D3D12_SHVER_LIBRARY = 6
        };

        #define D3D12_SHVER_GET_TYPE(_Version) (((_Version) >> 16) & 0xffff)

        using D3D_RESOURCE_RETURN_TYPE = UINT;
        using D3D_MIN_PRECISION = UINT;
        using D3D_PRIMITIVE = UINT;
        using D3D_PRIMITIVE_TOPOLOGY = UINT;
        using D3D_FEATURE_LEVEL = UINT;
        using D3D_TESSELLATOR_DOMAIN = UINT;
        using D3D_TESSELLATOR_PARTITIONING = UINT;
        using D3D_TESSELLATOR_OUTPUT_PRIMITIVE = UINT;

        struct D3D12_SIGNATURE_PARAMETER_DESC
        {
            LPCSTR                      SemanticName;
            UINT                        SemanticIndex;
            UINT                        Register;
            D3D_NAME                    SystemValueType;
            D3D_REGISTER_COMPONENT_TYPE ComponentType;
            BYTE                        Mask;
            BYTE                        ReadWriteMask;
            UINT                        Stream;
            D3D_MIN_PRECISION           MinPrecision;
        };

        struct D3D12_SHADER_BUFFER_DESC
        {
            LPCSTR           Name;
            D3D_CBUFFER_TYPE Type;
            UINT             Variables;
            UINT             Size;
            UINT             uFlags;
        };

        struct D3D12_SHADER_VARIABLE_DESC
        {
            LPCSTR Name;
            UINT   StartOffset;
            UINT   Size;
            UINT   uFlags;
            LPVOID DefaultValue;
            UINT   StartTexture;
            UINT   TextureSize;
            UINT   StartSampler;
            UINT   SamplerSize;
        };

        struct D3D12_SHADER_TYPE_DESC
        {
            D3D_SHADER_VARIABLE_CLASS Class;
            D3D_SHADER_VARIABLE_TYPE  Type;
            UINT                      Rows;
            UINT                      Columns;
            UINT                      Elements;
            UINT                      Members;
            UINT                      Offset;
            LPCSTR                    Name;
        };

        struct D3D12_SHADER_DESC
        {
            UINT                   Version;
            LPCSTR                 Creator;
            UINT                   Flags;
            UINT                   ConstantBuffers;
            UINT                   BoundResources;
            UINT                   InputParameters;
            UINT                   OutputParameters;
            UINT                   InstructionCount;
            UINT                   TempRegisterCount;
            UINT                   TempArrayCount;
            UINT                   DefCount;
            UINT                   DclCount;
            UINT                   TextureNormalInstructions;
            UINT                   TextureLoadInstructions;
            UINT                   TextureCompInstructions;
            UINT                   TextureBiasInstructions;
            UINT                   TextureGradientInstructions;
            UINT                   FloatInstructionCount;
            UINT                   IntInstructionCount;
            UINT                   UintInstructionCount;
            UINT                   StaticFlowControlCount;
            UINT                   DynamicFlowControlCount;
            UINT                   MacroInstructionCount;
            UINT                   ArrayInstructionCount;
            UINT                   CutInstructionCount;
            UINT                   EmitInstructionCount;
            D3D_PRIMITIVE_TOPOLOGY GSOutputTopology;
            UINT                   GSMaxOutputVertexCount;
            D3D_PRIMITIVE          InputPrimitive;
            UINT                   PatchConstantParameters;
            UINT                   cGSInstanceCount;
            UINT                   cControlPoints;
            D3D_TESSELLATOR_OUTPUT_PRIMITIVE HSOutputPrimitive;
            D3D_TESSELLATOR_PARTITIONING HSPartitioning;
            D3D_TESSELLATOR_DOMAIN TessellatorDomain;
            UINT                   cBarrierInstructions;
            UINT                   cInterlockedInstructions;
            UINT                   cTextureStoreInstructions;
        };

        struct D3D12_SHADER_INPUT_BIND_DESC
        {
            LPCSTR                   Name;
            D3D_SHADER_INPUT_TYPE    Type;
            UINT                     BindPoint;
            UINT                     BindCount;
            UINT                     uFlags;
            D3D_RESOURCE_RETURN_TYPE ReturnType;
            D3D_SRV_DIMENSION        Dimension;
            UINT                     NumSamples;
            UINT                     Space;
            UINT                     uID;
        };

        struct ID3D12ShaderReflectionType
        {
            virtual HRESULT STDMETHODCALLTYPE GetDesc(D3D12_SHADER_TYPE_DESC* pDesc) = 0;
            virtual ID3D12ShaderReflectionType* STDMETHODCALLTYPE GetMemberTypeByIndex(UINT Index) = 0;
            virtual ID3D12ShaderReflectionType* STDMETHODCALLTYPE GetMemberTypeByName(LPCSTR Name) = 0;
            virtual LPCSTR STDMETHODCALLTYPE GetMemberTypeName(UINT Index) = 0;
            virtual HRESULT STDMETHODCALLTYPE IsEqual(ID3D12ShaderReflectionType* pType) = 0;
            virtual ID3D12ShaderReflectionType* STDMETHODCALLTYPE GetSubType() = 0;
            virtual ID3D12ShaderReflectionType* STDMETHODCALLTYPE GetBaseClass() = 0;
            virtual UINT STDMETHODCALLTYPE GetNumInterfaces() = 0;
            virtual ID3D12ShaderReflectionType* STDMETHODCALLTYPE GetInterfaceByIndex(UINT uIndex) = 0;
            virtual HRESULT STDMETHODCALLTYPE IsOfType(ID3D12ShaderReflectionType* pType) = 0;
            virtual HRESULT STDMETHODCALLTYPE ImplementsInterface(ID3D12ShaderReflectionType* pBase) = 0;
            virtual ~ID3D12ShaderReflectionType() = default;
        };

        struct ID3D12ShaderReflectionConstantBuffer;

        struct ID3D12ShaderReflectionVariable
        {
            virtual HRESULT STDMETHODCALLTYPE GetDesc(D3D12_SHADER_VARIABLE_DESC* pDesc) = 0;
            virtual ID3D12ShaderReflectionType* STDMETHODCALLTYPE GetType() = 0;
            virtual ID3D12ShaderReflectionConstantBuffer* STDMETHODCALLTYPE GetBuffer() = 0;
            virtual UINT STDMETHODCALLTYPE GetInterfaceSlot(UINT uArrayIndex) = 0;
            virtual ~ID3D12ShaderReflectionVariable() = default;
        };

        struct ID3D12ShaderReflectionConstantBuffer
        {
            virtual HRESULT STDMETHODCALLTYPE GetDesc(D3D12_SHADER_BUFFER_DESC* pDesc) = 0;
            virtual ID3D12ShaderReflectionVariable* STDMETHODCALLTYPE GetVariableByIndex(UINT Index) = 0;
            virtual ID3D12ShaderReflectionVariable* STDMETHODCALLTYPE GetVariableByName(LPCSTR Name) = 0;
            virtual ~ID3D12ShaderReflectionConstantBuffer() = default;
        };

        struct ID3D12ShaderReflection : public IUnknown
        {
            virtual HRESULT STDMETHODCALLTYPE GetDesc(D3D12_SHADER_DESC* pDesc) = 0;
            virtual ID3D12ShaderReflectionConstantBuffer* STDMETHODCALLTYPE GetConstantBufferByIndex(UINT Index) = 0;
            virtual ID3D12ShaderReflectionConstantBuffer* STDMETHODCALLTYPE GetConstantBufferByName(LPCSTR Name) = 0;
            virtual HRESULT STDMETHODCALLTYPE GetResourceBindingDesc(UINT ResourceIndex, D3D12_SHADER_INPUT_BIND_DESC* pDesc) = 0;
            virtual HRESULT STDMETHODCALLTYPE GetInputParameterDesc(UINT ParameterIndex, D3D12_SIGNATURE_PARAMETER_DESC* pDesc) = 0;
            virtual HRESULT STDMETHODCALLTYPE GetOutputParameterDesc(UINT ParameterIndex, D3D12_SIGNATURE_PARAMETER_DESC* pDesc) = 0;
            virtual HRESULT STDMETHODCALLTYPE GetPatchConstantParameterDesc(UINT ParameterIndex, D3D12_SIGNATURE_PARAMETER_DESC* pDesc) = 0;
            virtual ID3D12ShaderReflectionVariable* STDMETHODCALLTYPE GetVariableByName(LPCSTR Name) = 0;
            virtual HRESULT STDMETHODCALLTYPE GetResourceBindingDescByName(LPCSTR Name, D3D12_SHADER_INPUT_BIND_DESC* pDesc) = 0;
            virtual UINT STDMETHODCALLTYPE GetMovInstructionCount() = 0;
            virtual UINT STDMETHODCALLTYPE GetMovcInstructionCount() = 0;
            virtual UINT STDMETHODCALLTYPE GetConversionInstructionCount() = 0;
            virtual UINT STDMETHODCALLTYPE GetBitwiseInstructionCount() = 0;
            virtual D3D_PRIMITIVE STDMETHODCALLTYPE GetGSInputPrimitive() = 0;
            virtual BOOL STDMETHODCALLTYPE IsSampleFrequencyShader() = 0;
            virtual UINT STDMETHODCALLTYPE GetNumInterfaceSlots() = 0;
            virtual HRESULT STDMETHODCALLTYPE GetMinFeatureLevel(D3D_FEATURE_LEVEL* pLevel) = 0;
            virtual UINT STDMETHODCALLTYPE GetThreadGroupSize(UINT* pSizeX, UINT* pSizeY, UINT* pSizeZ) = 0;
            virtual UINT64 STDMETHODCALLTYPE GetRequiresFlags() = 0;
        };

        #define DXC_CP_UTF8 65001
        #define DXC_CP_ACP 0

        typedef HRESULT (*DxcCreateInstanceProc)(REFCLSID rclsid, REFIID riid, LPVOID* ppv);

        inline constexpr CLSID CLSID_DxcCompiler = { 0x73e22d93, 0xe6ce, 0x47f3, { 0xb5, 0xbf, 0xf0, 0x66, 0x4f, 0x39, 0xc1, 0xb0 } };
        inline constexpr CLSID CLSID_DxcLibrary  = { 0x6245d6af, 0x66e0, 0x48fd, { 0x80, 0xb4, 0x4d, 0x27, 0x17, 0x96, 0x74, 0x8c } };
        inline constexpr CLSID CLSID_DxcUtils    = CLSID_DxcLibrary;
        inline constexpr CLSID CLSID_DxcContainerReflection = { 0xb9f54489, 0x55b8, 0x400c, { 0xba, 0x3a, 0x16, 0x75, 0xe4, 0x72, 0x8b, 0x91 } };
    #endif
#endif

namespace Morbi::Shader::Detail
{
#if defined(_WIN32)
    using DxcRefCountType = ULONG;
#else
    using DxcRefCountType = uint32_t;
#endif

    template <typename T>
    struct DxcInterfaceTraits;

#if defined(_WIN32)
    template <typename T>
    inline const IID& GetDxcInterfaceId() noexcept
    {
        return __uuidof(T);
    }
#else
    template <> struct DxcInterfaceTraits<IUnknown>
    {
        static inline constexpr IID Id = { 0x00000000, 0x0000, 0x0000, { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
    };
    template <> struct DxcInterfaceTraits<IDxcBlob>
    {
        static inline constexpr IID Id = { 0x8BA5FB08, 0x5195, 0x40E2, { 0xAC, 0x58, 0x0D, 0x98, 0x9C, 0x3A, 0x01, 0x02 } };
    };
    template <> struct DxcInterfaceTraits<IDxcBlobEncoding>
    {
        static inline constexpr IID Id = { 0x7241D424, 0x2646, 0x4191, { 0x97, 0xC0, 0x98, 0xE9, 0x6E, 0x42, 0xFC, 0x68 } };
    };
    template <> struct DxcInterfaceTraits<IDxcBlobUtf8>
    {
        static inline constexpr IID Id = { 0x3DA636C9, 0xBA71, 0x4024, { 0xA3, 0x01, 0x30, 0xCB, 0xF1, 0x25, 0x30, 0x5B } };
    };
    template <> struct DxcInterfaceTraits<IDxcIncludeHandler>
    {
        static inline constexpr IID Id = { 0x7F61FC7D, 0x950D, 0x467F, { 0xB3, 0xE3, 0x3C, 0x02, 0xFB, 0x49, 0x18, 0x7C } };
    };
    template <> struct DxcInterfaceTraits<IDxcResult>
    {
        static inline constexpr IID Id = { 0x58346CDA, 0xDDE7, 0x4497, { 0x94, 0x61, 0x6F, 0x87, 0xAF, 0x5E, 0x06, 0x59 } };
    };
    template <> struct DxcInterfaceTraits<IDxcUtils>
    {
        static inline constexpr IID Id = { 0x4605C4CB, 0x2019, 0x492A, { 0xAD, 0xA4, 0x65, 0xF2, 0x0B, 0xB7, 0xD6, 0x7F } };
    };
    template <> struct DxcInterfaceTraits<IDxcCompiler3>
    {
        static inline constexpr IID Id = { 0x228B4687, 0x5A6A, 0x4730, { 0x90, 0x0C, 0x97, 0x02, 0xB2, 0x20, 0x3F, 0x54 } };
    };
    template <> struct DxcInterfaceTraits<IDxcVersionInfo>
    {
        static inline constexpr IID Id = { 0xB04F5B50, 0x2059, 0x4F12, { 0xA8, 0xFF, 0xA1, 0xE0, 0xCD, 0xE1, 0xCC, 0x7E } };
    };
    template <> struct DxcInterfaceTraits<IDxcVersionInfo2>
    {
        static inline constexpr IID Id = { 0xFB6904C4, 0x42F0, 0x4B62, { 0x9C, 0x46, 0x98, 0x3A, 0xF7, 0xDA, 0x7C, 0x83 } };
    };
    template <> struct DxcInterfaceTraits<IDxcVersionInfo3>
    {
        static inline constexpr IID Id = { 0x5E13E843, 0x9D25, 0x473C, { 0x9A, 0xD2, 0x03, 0xB2, 0xD0, 0xB4, 0x4B, 0x1E } };
    };
    template <> struct DxcInterfaceTraits<IDxcContainerReflection>
    {
        static inline constexpr IID Id = { 0xD2C21B26, 0x8350, 0x4BDC, { 0x97, 0x6A, 0x33, 0x1C, 0xE6, 0xF4, 0xC5, 0x4C } };
    };
    template <> struct DxcInterfaceTraits<ID3D12ShaderReflection>
    {
        static inline constexpr IID Id = { 0x5A58797D, 0xA72C, 0x478D, { 0x8B, 0xA2, 0xEF, 0xC6, 0xB0, 0xEF, 0xE8, 0x8E } };
    };

    template <typename T>
    inline const IID& GetDxcInterfaceId() noexcept
    {
        return DxcInterfaceTraits<T>::Id;
    }
#endif

    template <typename T>
    class DxcComPtr
    {
    public:
        DxcComPtr() noexcept = default;
        DxcComPtr(std::nullptr_t) noexcept {}

        DxcComPtr(const DxcComPtr& other) noexcept
            : m_ptr(other.m_ptr)
        {
            if (m_ptr)
            {
                m_ptr->AddRef();
            }
        }

        DxcComPtr(DxcComPtr&& other) noexcept
            : m_ptr(std::exchange(other.m_ptr, nullptr))
        {
        }

        ~DxcComPtr()
        {
            Reset();
        }

        DxcComPtr& operator=(const DxcComPtr& other) noexcept
        {
            if (this != &other)
            {
                DxcComPtr tmp(other);
                Swap(tmp);
            }
            return *this;
        }

        DxcComPtr& operator=(DxcComPtr&& other) noexcept
        {
            if (this != &other)
            {
                Reset();
                m_ptr = std::exchange(other.m_ptr, nullptr);
            }
            return *this;
        }

        DxcComPtr& operator=(std::nullptr_t) noexcept
        {
            Reset();
            return *this;
        }

        void Reset() noexcept
        {
            if (m_ptr)
            {
                T* ptr = std::exchange(m_ptr, nullptr);
                ptr->Release();
            }
        }

        void Attach(T* ptr) noexcept
        {
            Reset();
            m_ptr = ptr;
        }

        [[nodiscard]] T* Detach() noexcept
        {
            return std::exchange(m_ptr, nullptr);
        }

        [[nodiscard]] T* Get() const noexcept { return m_ptr; }
        [[nodiscard]] T* operator->() const noexcept { return m_ptr; }
        [[nodiscard]] explicit operator bool() const noexcept { return m_ptr != nullptr; }

        [[nodiscard]] T** GetAddressOf() noexcept
        {
            Reset();
            return &m_ptr;
        }

        [[nodiscard]] void** PutVoid() noexcept
        {
            Reset();
            return reinterpret_cast<void**>(&m_ptr);
        }

        void Swap(DxcComPtr& other) noexcept
        {
            std::swap(m_ptr, other.m_ptr);
        }

    private:
        T* m_ptr = nullptr;
    };

    inline void FreeDxcTaskString(char* str) noexcept
    {
        if (!str)
        {
            return;
        }
#if defined(_WIN32)
        CoTaskMemFree(str);
#else
        std::free(str);
#endif
    }
}
