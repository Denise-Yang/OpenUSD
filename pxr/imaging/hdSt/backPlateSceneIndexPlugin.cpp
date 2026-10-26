//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
#include "hdPrman/backPlateSceneIndexPlugin.h"

#include "pxr/imaging/hd/version.h"

// There was no hdsi/version.h before this HD_API_VERSION.
#if HD_API_VERSION >= 58

#include "pxr/imaging/hdsi/version.h"

#if HDSI_API_VERSION >= 19
#define HDPRMAN_USE_PARTICLE_FIELD_CONVERSION_SCENE_INDEX
#endif

#endif // #if HD_API_VERSION >= 58

#include "hdPrman/tokens.h"
#include "pxr/usd/usdVol/tokens.h"
#include "pxr/usdImaging/usdImaging/tokens.h"
#include "pxr/imaging/hd/sceneIndexPluginRegistry.h"
#include "pxr/imaging/hd/filteringSceneIndex.h"
#include "pxr/imaging/hd/retainedDataSource.h"
#include "pxr/imaging/hd/materialSchema.h"
#include "pxr/imaging/hd/materialNetworkSchema.h"
#include "pxr/imaging/hd/materialNodeSchema.h"
#include "pxr/imaging/hd/materialNodeParameterSchema.h"
#include "pxr/imaging/hd/materialConnectionSchema.h"
#include "pxr/imaging/hd/materialBindingsSchema.h"
#include "pxr/imaging/hd/materialBindingSchema.h"
#include "pxr/imaging/hd/meshSchema.h"
#include "pxr/imaging/hd/meshTopologySchema.h"
#include "pxr/imaging/hd/overlayContainerDataSource.h"
#include "pxr/imaging/hd/primvarsSchema.h"
#include "pxr/imaging/hd/primvarSchema.h"
#include "pxr/imaging/hd/schemaTypeDefs.h"
#include "pxr/imaging/hd/tokens.h"
#include "pxr/imaging/hd/xformSchema.h"
#include "pxr/imaging/hd/tokens.h"

#include "pxr/imaging/hdsi/backPlateSceneIndex.h"

#include <array>

#include "pxr/imaging/pxOsd/tokens.h"

PXR_NAMESPACE_OPEN_SCOPE

static constexpr int _flatPlateSubdivs = 1;
TF_DEFINE_PRIVATE_TOKENS(_backPlatePrimsTokens,
    (mesh)
    (material)
);
TF_DEFINE_PRIVATE_TOKENS(_materialNodeNamesTokens,
    (UsdPrimvarReader_float2)
    (UsdUVTexture)
    (DepthTexture)
    (PxrConstant)
    (BackPlateAlpha)
);

TF_DEFINE_PRIVATE_TOKENS(_backPlateAttrTokens,
    (image)
    (alphaImage)
    (depthImage)
    (power)
    (slope)
    (offset)
    (depthMinOffset)
    (depthNormalizingFactor)
    (depthCameraSpaceOffset)
);

TF_DEFINE_PRIVATE_TOKENS(_materialNodeParamsTokens,
    (rgb)
    (a)
    (ri)
    (presence)
    (clamp)
    (file)
    (st)
    (wrapS)
    (wrapT)
    (implementationSource)
    (emitColor)
    (varname)
    (result)
);

TF_REGISTRY_FUNCTION(TfType)
{
    HdSceneIndexPluginRegistry::Define<HdPrman_BackPlateSceneIndexPlugin>();
}

TF_REGISTRY_FUNCTION(HdSceneIndexPlugin)
{
    const HdSceneIndexPluginRegistry::InsertionPhase insertionPhase = 0;

    for (const auto& pluginDisplayName : HdPrman_GetPluginDisplayNames()) {
        HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(
            pluginDisplayName, HdPrmanPluginTokens->backPlate, nullptr,
            insertionPhase, HdSceneIndexPluginRegistry::InsertionOrderAtStart);
    }
}

HdPrman_BackPlateSceneIndexPlugin::HdPrman_BackPlateSceneIndexPlugin() = default;

class HdPrmanBackPlateSceneIndex : public HdsiBackPlateSceneIndex
{
public:
    static HdsiBackPlateSceneIndexRefPtr
    New(const HdSceneIndexBaseRefPtr &inputSceneIndex)
    {
        return TfCreateRefPtr(
            new HdPrmanBackPlateSceneIndex(inputSceneIndex));
    }

protected:
    HdPrmanBackPlateSceneIndex(
        const HdSceneIndexBaseRefPtr &inputSceneIndex)
        : HdsiBackPlateSceneIndex(inputSceneIndex) {}

/////////////////////////// DataSource Builders ///////////////////////////////
    class _AssetPathDataSource : public HdAssetPathDataSource 
    {
    public:
        HD_DECLARE_DATASOURCE(_AssetPathDataSource);

        VtValue GetValue(Time t) override {
            return VtValue(GetTypedValue(t));
        }

        SdfAssetPath GetTypedValue(Time shutterOffset) override {
            HdBackPlateSchema backPlate = 
                HdsiBackPlateSceneIndex::_GetBackPlateSchema(_instanceName, 
                                                             _primDS);
            SdfAssetPath assetPath;
            if (!HdsiBackPlateSceneIndex::_GetDefaultTypedValue(
                    _GetAssetType(backPlate), shutterOffset, &assetPath)){
                return SdfAssetPath();
            };
            return assetPath;
        }

        bool GetContributingSampleTimesForInterval(
                Time startTime, Time endTime,
                std::vector<Time> *outSampleTimes) override
        {
            HdBackPlateSchema backPlate = 
                HdsiBackPlateSceneIndex::_GetBackPlateSchema(_instanceName, 
                                                             _primDS);
            HdSampledDataSourceHandle source = _GetAssetType(backPlate);
            if (!source) {
                return false;
            }
            return source->GetContributingSampleTimesForInterval(
                startTime, endTime, outSampleTimes);
        }

    private:
        _AssetPathDataSource(TfToken const &instanceName,
                            TfToken const & assetType,
                            HdContainerDataSourceHandle const &primDS)
        : _instanceName(instanceName)
        , _assetType(assetType)
        , _primDS(primDS){}

        HdAssetPathDataSourceHandle 
        _GetAssetType(HdBackPlateSchema const &backPlate) const {
            if (_assetType == _backPlateAttrTokens->image) {
                return backPlate.GetImage();
            }
            if (_assetType == _backPlateAttrTokens->depthImage) {
                return backPlate.GetDepthImage();
            }
            if (_assetType == _backPlateAttrTokens->alphaImage) {
                return backPlate.GetAlphaImage();
            }
            return nullptr;
        }

        TfToken const _instanceName;
        TfToken const _assetType;
        HdContainerDataSourceHandle const _primDS;
    };

    class _DepthVarDataSource : public HdFloatDataSource 
    {
    public:
        HD_DECLARE_DATASOURCE(_DepthVarDataSource);

        VtValue GetValue(Time t) override {
            return VtValue(GetTypedValue(t));
        }

        float GetTypedValue(Time shutterOffset) override {
            HdBackPlateSchema backPlate = 
                HdsiBackPlateSceneIndex::_GetBackPlateSchema(_instanceName, 
                                                             _primDS);
            float result(0.0);
            // if _type is depthNormalizingFactor we change the default to 1.0.
            if (_type == _backPlateAttrTokens->depthNormalizingFactor) {
                result = 1.0;
            }

            if (!HdsiBackPlateSceneIndex::_GetDefaultTypedValue(
                    _GetVarType(backPlate), shutterOffset, &result)) {
                return result;
            }
            return result;
        }

        bool GetContributingSampleTimesForInterval(
                Time startTime, Time endTime,
                std::vector<Time> *outSampleTimes) override
        {
            HdBackPlateSchema backPlate = 
                HdsiBackPlateSceneIndex::_GetBackPlateSchema(_instanceName, 
                                                             _primDS);
            HdSampledDataSourceHandle source = _GetVarType(backPlate);
            if (!source) {
                return false;
            }
            return source->GetContributingSampleTimesForInterval(
                startTime, endTime, outSampleTimes);
        }

    private:
        _DepthVarDataSource(TfToken const &instanceName,
                            TfToken const & type,
                            HdContainerDataSourceHandle const &primDS)
        : _instanceName(instanceName)
        , _type(type)
        , _primDS(primDS){}

        HdFloatDataSourceHandle 
        _GetVarType(HdBackPlateSchema const &backPlate) const {
            if (_type == _backPlateAttrTokens->depthMinOffset) {
                return backPlate.GetDepthMinOffset();
            }
            if (_type == _backPlateAttrTokens->depthNormalizingFactor) {
                return backPlate.GetDepthNormalizingFactor();
            }
            if (_type == _backPlateAttrTokens->depthCameraSpaceOffset) {
                return backPlate.GetDepthCameraSpaceOffset();
            }
            
            return nullptr;
        }

        TfToken const _instanceName;
        TfToken const _type;
        HdContainerDataSourceHandle const _primDS;
    };

    class _ColorGradingDataSource : public HdVec3fDataSource 
    {
    public:
        HD_DECLARE_DATASOURCE(_ColorGradingDataSource);

        VtValue GetValue(Time t) override {
            return VtValue(GetTypedValue(t));
        }

        GfVec3f GetTypedValue(Time shutterOffset) override {
            HdBackPlateSchema backPlate = 
                HdsiBackPlateSceneIndex::_GetBackPlateSchema(_instanceName, 
                                                             _primDS);
            GfVec3f result(0.0);
            // if _type is offset we leave the default as (0, 0, 0).
            if (_type == _backPlateAttrTokens->slope || 
                _type == _backPlateAttrTokens->power) {
                result = GfVec3f(1.0);
            }

            if (!HdsiBackPlateSceneIndex::_GetDefaultTypedValue(
                    _GetLumaSource(backPlate), shutterOffset, &result)) {
                return result;
            }
            return result;
        }

        bool GetContributingSampleTimesForInterval(
                Time startTime, Time endTime,
                std::vector<Time> *outSampleTimes) override
        {
            HdBackPlateSchema backPlate = 
                HdsiBackPlateSceneIndex::_GetBackPlateSchema(_instanceName, 
                                                             _primDS);
            HdSampledDataSourceHandle source = _GetLumaSource(backPlate);
            if (!source) {
                return false;
            }
            return source->GetContributingSampleTimesForInterval(
                startTime, endTime, outSampleTimes);
        }

    private:
        _ColorGradingDataSource(TfToken const &instanceName,
                                TfToken const & type,
                                HdContainerDataSourceHandle const &primDS)
        : _instanceName(instanceName)
        , _type(type)
        , _primDS(primDS){}

        HdVec3fDataSourceHandle 
        _GetLumaSource(HdBackPlateSchema const &backPlate) const {
            if (_type == _backPlateAttrTokens->offset) {
                return backPlate.GetLumaOffset();
            }
            if (_type == _backPlateAttrTokens->slope) {
                return backPlate.GetLumaSlope();
            }
            if (_type == _backPlateAttrTokens->power) {
                return backPlate.GetLumaPower();
            }
            return nullptr;
        }

        TfToken const _instanceName;
        TfToken const _type;
        HdContainerDataSourceHandle const _primDS;
    };

    class _PointsValueDataSource : public HdVec3fArrayDataSource
    {
    public:
        HD_DECLARE_DATASOURCE(_PointsValueDataSource);

        VtValue GetValue(Time t) override {
            return VtValue(GetTypedValue(t));
        }

        VtVec3fArray GetTypedValue(Time t) override {
            GfVec2f dims = HdsiBackPlateSceneIndex::_ComputeEffectiveDimensions( 
                                _instanceName, _primDS, t);
            float width = dims[0];
            float length = dims[1];
            const int N = _flatPlateSubdivs; 
            const int dim  = N + 1;
            VtVec3fArray pts(dim*dim);
            for (int r = 0; r < dim; ++r) {
                for (int i = 0; i < dim; ++i) {
                    const float u = float(i) / N;
                    const float v = float(r) / N;
                    pts[r*dim + i] = 
                        GfVec3f((u - 0.5f) * width, (v - 0.5f) * length, 0.0f);
                }
            }
            return pts;
        }

        bool GetContributingSampleTimesForInterval(
            Time startTime, Time endTime,
            std::vector<Time> *outSampleTimes) override
        {
            HdCameraSchema cam = HdCameraSchema::GetFromParent(_primDS);
            HdBackPlateSchema backPlate = 
                HdsiBackPlateSceneIndex::_GetBackPlateSchema(_instanceName, 
                                                             _primDS);
            HdSampledDataSourceHandle sources[] = {
                cam.GetHorizontalAperture(),
                cam.GetVerticalAperture(),
                cam.GetFocalLength(),
                cam.GetFocusDistance(),
                cam.GetProjection(),
                backPlate.GetScaleTweak()
            };
            return HdGetMergedContributingSampleTimesForInterval(
                TfArraySize(sources),
                sources, 
                startTime, 
                endTime, 
                outSampleTimes);
        }

    private:
        _PointsValueDataSource(TfToken const &instanceName,
                            HdContainerDataSourceHandle const &primDS,
                            bool const hasDepth)
        : _instanceName(instanceName)
        , _primDS(primDS)
        , _hasDepth(hasDepth) {}

        TfToken const _instanceName;
        HdContainerDataSourceHandle const _primDS;
        bool const _hasDepth;
    }; 

    class _XformMatrixDataSource : public HdMatrixDataSource
    {
    public:
        HD_DECLARE_DATASOURCE(_XformMatrixDataSource);

        VtValue GetValue(Time t) override {
            return VtValue(GetTypedValue(t));
        }

        GfMatrix4d GetTypedValue(Time shutterOffset) override {
            HdXformSchema camXform = HdXformSchema::GetFromParent(
                _primDS);

            const GfMatrix4d local = 
                HdsiBackPlateSceneIndex::_ComputeLocalXform(
                    _primDS, _instanceName, shutterOffset);
            GfMatrix4d camXformMatrix(1.0);

            if (camXform) {
                HdsiBackPlateSceneIndex::_GetDefaultTypedValue(
                    camXform.GetMatrix(), shutterOffset, &camXformMatrix);
            }
            return local * camXformMatrix;
        }

        bool GetContributingSampleTimesForInterval(
                Time startTime, Time endTime,
                std::vector<Time> *outSampleTimes) override
        {
            HdCameraSchema cam = 
                HdCameraSchema::GetFromParent(_primDS);
            HdXformSchema camXform = 
                HdXformSchema::GetFromParent(_primDS);
            HdBackPlateSchema backPlate = 
                HdsiBackPlateSceneIndex::_GetBackPlateSchema(_instanceName, 
                                                             _primDS);
            HdSampledDataSourceHandle sources[] = {
                cam.GetFocusDistance(),
                backPlate.GetTranslateTweak(),
                backPlate.GetRotateXYZTweak(),
                camXform.GetMatrix()
            };
            return HdGetMergedContributingSampleTimesForInterval(
                TfArraySize(sources), 
                sources, 
                startTime, 
                endTime, 
                outSampleTimes);
        }           

    private:
        _XformMatrixDataSource(TfToken const &instanceName,
                                HdContainerDataSourceHandle const &primDS)
        : _instanceName(instanceName)
        , _primDS(primDS) {}

        TfToken const _instanceName;
        HdContainerDataSourceHandle const _primDS;
    };

    static HdContainerDataSourceHandle
    _BuildMeshTopologyDataSource(const bool hasDepth) 
    {
        VtIntArray numVerts;
        VtIntArray verts;
        const int N = _flatPlateSubdivs;
        const int dim = N+1;
        numVerts = VtIntArray(N*N, 4);
        verts.reserve(N * N * 4);
        for (int r = 0; r < N; ++r) {
            for (int i = 0; i < N; ++i) {
                const int botL = r*dim + i;
                const int botR = r*dim + i + 1;
                const int topL = (r+1)*dim + i;
                const int topR = (r+1)*dim + i + 1;
                verts.insert(verts.end(), {topR, topL, botL, botR});
            }
        }

        HdContainerDataSourceHandle topologyDs =
            HdMeshTopologySchema::Builder()
                .SetFaceVertexCounts(
                    HdRetainedTypedSampledDataSource<VtIntArray>::New(numVerts))
                .SetFaceVertexIndices(
                    HdRetainedTypedSampledDataSource<VtIntArray>::New(verts))
                .SetOrientation(
                    HdRetainedTypedSampledDataSource<TfToken>::New(
                        HdMeshTopologySchemaTokens->rightHanded))
                .Build();
        return topologyDs;
    }

    class _PrimvarsDataSource : public HdContainerDataSource
    {
    public:
        HD_DECLARE_DATASOURCE(_PrimvarsDataSource);

        TfTokenVector GetNames() override {
            static const TfTokenVector result
                {HdPrimvarsSchemaTokens->points, 
                _materialNodeParamsTokens->st};
            return result;
        }

        HdDataSourceBaseHandle Get(const TfToken &name) override {
            if (name == HdPrimvarsSchemaTokens->points) {
                HdContainerDataSourceHandle pointsPrimvarDs =
                HdPrimvarSchema::Builder()
                    .SetPrimvarValue(
                        _PointsValueDataSource::New(
                            _instanceName, _primDS, _hasDepth))
                    .SetInterpolation(
                        HdRetainedTypedSampledDataSource<TfToken>::New(
                            HdPrimvarSchemaTokens->vertex))
                    .SetRole(
                        HdRetainedTypedSampledDataSource<TfToken>::New(
                            HdPrimvarSchemaTokens->point))
                    .Build();
                return pointsPrimvarDs;
            }
            if (name == _materialNodeParamsTokens->st) {
                const int N = _flatPlateSubdivs;
                const int dim = N+1;
                VtVec2fArray st(dim*dim);
                for (int r = 0; r < dim; ++r) {
                    for (int i = 0; i < dim; ++i) {
                        st[r*dim + i] = GfVec2f(float(i)/N, float(r)/N);
                    }
                }

                const HdContainerDataSourceHandle stPrimvarDs =
                    HdPrimvarSchema::Builder()
                    .SetPrimvarValue(
                        HdRetainedTypedSampledDataSource<VtVec2fArray>::New(
                            st))
                    .SetInterpolation(
                        HdRetainedTypedSampledDataSource<TfToken>::New(
                            HdPrimvarSchemaTokens->vertex))
                    .SetRole(
                        HdRetainedTypedSampledDataSource<TfToken>::New(
                            TfToken()))
                    .Build();
                return stPrimvarDs;
            }
            return nullptr;
        }

    private:
        _PrimvarsDataSource(
            TfToken const & instanceName,
            HdContainerDataSourceHandle const &primDS,
            bool const &hasDepth)
        : _instanceName(instanceName),
        _primDS(primDS),
        _hasDepth(hasDepth)
        {
        }
        TfToken const _instanceName;
        HdContainerDataSourceHandle const _primDS;
        bool const _hasDepth;
    };

    static HdDataSourceBaseHandle
    _BuildXformDataSource(
        const TfToken &instanceName,
        const HdContainerDataSourceHandle &primDS) {

        HdDataSourceBaseHandle xformDs = HdXformSchema::Builder()
            .SetMatrix(
                _XformMatrixDataSource::New(instanceName, primDS))
            .SetResetXformStack(
                HdRetainedTypedSampledDataSource<bool>::New(false))
            .Build();
        return xformDs;
    }

    static HdContainerDataSourceHandle
    _BuildMaterialBindingsDataSource(const SdfPath &platePath)
    {
        SdfPath materialPath =
            platePath.AppendChild(_backPlatePrimsTokens->material);
        HdDataSourceBaseHandle materialBindingDs = 
                    HdMaterialBindingSchema::Builder()
                        .SetPath(HdRetainedTypedSampledDataSource<SdfPath>::New(
                            materialPath))
                        .Build();
        return HdRetainedContainerDataSource::New(
                HdMaterialBindingsSchemaTokens->allPurpose,
                materialBindingDs);
    }

    static HdContainerDataSourceHandle
    _BuildMeshDataSource(const bool hasDepth)
    {
        return HdRetainedContainerDataSource::New( 
            HdMeshSchemaTokens->topology,
                _BuildMeshTopologyDataSource(hasDepth),
            HdMeshSchemaTokens->subdivisionScheme,
                HdRetainedTypedSampledDataSource<TfToken>::New(
                    PxOsdOpenSubdivTokens->catmullClark),
            HdMeshSchemaTokens->doubleSided,
                HdRetainedTypedSampledDataSource<bool>::New(false));
    }

    static HdContainerDataSourceHandle
    _BuildMaterialNetworkDataSource (
        TfToken const &appliedInstanceName,
        HdContainerDataSourceHandle const &primDS)
    {
        // USD Primar Reader Params
        static const HdDataSourceBaseHandle usdPrimvarReaderParams = {
            HdMaterialNodeParameterSchema::Builder()
                .SetValue(HdRetainedTypedSampledDataSource<VtValue>::New(
                    VtValue(_materialNodeParamsTokens->st)))
                .Build()};

        // USD Primar Reader Node
        const HdDataSourceBaseHandle usdPrimvarReaderNode
            = HdMaterialNodeSchema::Builder()
                .SetParameters(
                    HdRetainedContainerDataSource::New(
                        _materialNodeParamsTokens->varname,
                        usdPrimvarReaderParams))
                .SetNodeIdentifier(
                    HdRetainedTypedSampledDataSource<TfToken>::New(
                        _materialNodeNamesTokens->UsdPrimvarReader_float2))
                .Build();

        /// Usd UVTextureNode Params
        static const TfToken texParamNames[] = { 
            _materialNodeParamsTokens->file, 
            _materialNodeParamsTokens->st, 
            _materialNodeParamsTokens->wrapS, 
            _materialNodeParamsTokens->wrapT };
        _AssetPathDataSource::Handle assetPathDs =_AssetPathDataSource::New(
            appliedInstanceName, _backPlateAttrTokens->image, primDS);
        HdDataSourceBaseHandle texParams[] = {
            HdMaterialNodeParameterSchema::Builder()
                .SetValue(assetPathDs)
                .Build(),
            HdMaterialNodeParameterSchema::Builder()
                .SetValue(HdRetainedTypedSampledDataSource<TfToken>::New(
                    _materialNodeParamsTokens->st))
                .Build(),
            HdMaterialNodeParameterSchema::Builder()
                .SetValue(HdRetainedTypedSampledDataSource<TfToken>::New(
                    _materialNodeParamsTokens->clamp))
                .Build(),
            HdMaterialNodeParameterSchema::Builder()
                .SetValue(HdRetainedTypedSampledDataSource<TfToken>::New(
                    _materialNodeParamsTokens->clamp))
                .Build()};

        // Usd UVTextureNode Connections
        static const TfToken texConnectionNames[] = 
            {_materialNodeParamsTokens->st};
        HdDataSourceBaseHandle texConnections[] = {
            HdRetainedSmallVectorDataSource::New(
                1,
                std::array<HdDataSourceBaseHandle, 1>{
                    HdMaterialConnectionSchema::Builder()
                        .SetUpstreamNodePath(
                            HdRetainedTypedSampledDataSource<TfToken>::New(
                                _materialNodeNamesTokens->UsdPrimvarReader_float2))
                        .SetUpstreamNodeOutputName(
                            HdRetainedTypedSampledDataSource<TfToken>::New(
                                _materialNodeParamsTokens->result))
                        .Build()
                }.data())
        };
        // UsdUVTextureNode 
        HdDataSourceBaseHandle textureNode =
            HdMaterialNodeSchema::Builder()
            .SetNodeIdentifier(
                HdRetainedTypedSampledDataSource<TfToken>::New(
                    _materialNodeNamesTokens->UsdUVTexture))
            .SetInputConnections(
                HdRetainedContainerDataSource::New(
                    1, texConnectionNames, texConnections))
            .SetParameters(
                HdRetainedContainerDataSource::New(4, texParamNames, texParams))
            .Build();
        const HdDataSourceBaseHandle surfaceConnection = {
            HdRetainedSmallVectorDataSource::New(
                1,
                std::array<HdDataSourceBaseHandle, 1> {
                    HdMaterialConnectionSchema::Builder()
                        .SetUpstreamNodePath(
                            HdRetainedTypedSampledDataSource<TfToken>::New(
                                _materialNodeNamesTokens->UsdUVTexture))
                        .SetUpstreamNodeOutputName(
                            HdRetainedTypedSampledDataSource<TfToken>::New(
                                _materialNodeParamsTokens->rgb))
                        .Build()}.data())};
        static const TfToken pxrConstantConnectionNames[] = {
            _materialNodeParamsTokens->emitColor,
        };
\
        static const HdDataSourceBaseHandle pxrConstantConnections[] = {
            surfaceConnection,
        };

        const int connNum = 1;

        const HdDataSourceBaseHandle pxrConstantNode = 
        HdMaterialNodeSchema::Builder()
            .SetInputConnections(HdRetainedContainerDataSource::New(
                connNum, pxrConstantConnectionNames, pxrConstantConnections))
            .SetNodeIdentifier(HdRetainedTypedSampledDataSource<TfToken>::New(
                _materialNodeNamesTokens->PxrConstant))
        
            .Build(); 

        // USD Unlit Surface Nodes
        const HdContainerDataSourceHandle nodesDs
            = HdRetainedContainerDataSource::New(
                _materialNodeNamesTokens->UsdPrimvarReader_float2, 
                    usdPrimvarReaderNode,
                _materialNodeNamesTokens->PxrConstant, pxrConstantNode,
                _materialNodeNamesTokens->UsdUVTexture, textureNode);

        // USD Unlit Surface Terminals
        static const HdContainerDataSourceHandle terminalsDs
            = HdRetainedContainerDataSource::New(
                HdMaterialTerminalTokens->surface,
                HdMaterialConnectionSchema::Builder()
                    .SetUpstreamNodePath(
                        HdRetainedTypedSampledDataSource<TfToken>::New(
                            _materialNodeNamesTokens->PxrConstant))
                    .SetUpstreamNodeOutputName(
                        HdRetainedTypedSampledDataSource<TfToken>::New(
                            HdMaterialTerminalTokens->surface))
                    .Build()
        );

        // PxrConstant Material Network
        const HdDataSourceBaseHandle materialNetworkDs
            = HdMaterialNetworkSchema::Builder()
                .SetNodes(nodesDs)
                .SetTerminals(terminalsDs)
                .Build();

        // PxrConstant Material
        const HdContainerDataSourceHandle materialDs
            = HdMaterialSchema::BuildRetained(
                1,
                &_materialNodeParamsTokens->ri,
                &materialNetworkDs
        );
        return materialDs;
    }

    class _MaterialDataSource : public HdContainerDataSource
    {
    public:
        HD_DECLARE_DATASOURCE(_MaterialDataSource);

        TfTokenVector GetNames() override {
            static const TfTokenVector result = {
                HdMaterialSchemaTokens->material
            };
            return result;
        }

        HdDataSourceBaseHandle Get(const TfToken &name) override {
            if (name == HdMaterialSchemaTokens->material) {
                return _BuildMaterialNetworkDataSource(_instanceName,
                                                       _primDS);
            }
            return nullptr;
        }

    private:
        _MaterialDataSource(
            TfToken const & instanceName,
            HdContainerDataSourceHandle const &primDS)
        : _instanceName(instanceName),
        _primDS(primDS)
        {}
        TfToken const _instanceName;
        HdContainerDataSourceHandle const _primDS;
    };

////////////////////////////// HDST Overrides /////////////////////////////////
    HdContainerDataSourceHandle
    _BuildMaterialDataSource(
        const TfToken &instanceName,
        const HdContainerDataSourceHandle &primDS) const override
    {
        return _MaterialDataSource::New(instanceName, primDS);
    }
    
    HdContainerDataSourceHandle
    _BuildMeshPrimDataSource(
        const SdfPath &platePath,
        const TfToken &instanceName,
        const HdContainerDataSourceHandle &primDS) const override
    {
        HdBackPlateSchema backPlate =
            HdsiBackPlateSceneIndex::_GetBackPlateSchema(
                instanceName, primDS);
        bool hasDepth = false;
        if (backPlate) {
            SdfAssetPath depthAsset;
            HdsiBackPlateSceneIndex::_GetDefaultTypedValue(
                backPlate.GetDepthImage(), 0.0, &depthAsset);
            hasDepth = depthAsset != SdfAssetPath();
        }
        return
            HdRetainedContainerDataSource::New(
                HdMeshSchemaTokens->mesh,
                    _BuildMeshDataSource(hasDepth),
                HdPrimvarsSchemaTokens->primvars,
                    _PrimvarsDataSource::New(instanceName, primDS, hasDepth),
                HdXformSchemaTokens->xform,
                    _BuildXformDataSource(instanceName, primDS),
                HdMaterialBindingsSchema::GetSchemaToken(),
                    _BuildMaterialBindingsDataSource(platePath));
    }
};

HdSceneIndexBaseRefPtr
HdPrman_BackPlateSceneIndexPlugin::_AppendSceneIndex(
    const HdSceneIndexBaseRefPtr& inputScene,
    const HdContainerDataSourceHandle& inputArgs)
{
    return HdPrmanBackPlateSceneIndex::New(inputScene);
}

PXR_NAMESPACE_CLOSE_SCOPE

