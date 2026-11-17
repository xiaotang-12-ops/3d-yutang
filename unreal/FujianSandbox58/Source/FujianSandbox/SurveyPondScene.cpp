// A屏原山脉上的示意渔场：按原经纬度一次性裁分并削平局部山地，建立三分塘与薄埂。
// 缓存原节以便实例回收；不改DEM/共享坐标、不隐藏山脉，不将示意尺寸冒充实测塘界。-codex，2026-10-30
#include "FujianMap.h"
#include "XinluoSurveyData.h"
#include "ProceduralMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Materials/MaterialInterface.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "PhysicsEngine/BodySetup.h"

namespace SurveyFarmGeometry
{
    using FPolygon = TArray<FProcMeshVertex>;
    constexpr double Epsilon=1.e-10;
    struct FFarm
    {
        FString Id;
        FVector Center=FVector::ZeroVector;
        FVector WaterAnchor=FVector::ZeroVector;
        FVector2D WaterHalfExtent=FVector2D::ZeroVector;
        float Radius=0;
        double OriginalCenterZ=0;
        int32 ChangedTriangles=0;
        double Half() const { return Radius*.70; }
        double Bed() const { return Center.Z-Radius*.022; }
        bool Overlaps(const FVector& A,const FVector& B,const FVector& C) const
        {
            const double H=Half();
            return FMath::Min3(A.X,B.X,C.X)<Center.X+H&&FMath::Max3(A.X,B.X,C.X)>Center.X-H&&
                FMath::Min3(A.Y,B.Y,C.Y)<Center.Y+H&&FMath::Max3(A.Y,B.Y,C.Y)>Center.Y-H;
        }
        double Height(const FVector& Original) const
        {
            const double Q=FMath::Max(FMath::Abs(Original.X-Center.X),FMath::Abs(Original.Y-Center.Y))/Half();
            // 核心平底低于不透明水面，外框严格返回原地形；核心和过渡带边界均参与裁分。-codex，2026-10-30
            if(Q>=1.-Epsilon)return Original.Z;
            const double T=FMath::Clamp((Q-.72)/.28,0.,1.);
            return FMath::Lerp(Bed(),Original.Z,T*T*(3.-2.*T));
        }
    };
    struct FTerrain
    {
        TWeakObjectPtr<UProceduralMeshComponent> Mesh;
        TWeakObjectPtr<UProceduralMeshComponent> LocalMesh;
        FProcMeshSection Original,Static,LocalOriginal;
    };
    FProcMeshVertex Interpolate(const FProcMeshVertex& A,const FProcMeshVertex& B,double T)
    {
        FProcMeshVertex V;
        V.Position=FMath::Lerp(A.Position,B.Position,T);
        V.Normal=FMath::Lerp(A.Normal,B.Normal,T).GetSafeNormal();
        V.Tangent=FProcMeshTangent(FMath::Lerp(A.Tangent.TangentX,B.Tangent.TangentX,T).GetSafeNormal(),A.Tangent.bFlipTangentY);
        V.UV0=FMath::Lerp(A.UV0,B.UV0,T);V.UV1=FMath::Lerp(A.UV1,B.UV1,T);
        V.UV2=FMath::Lerp(A.UV2,B.UV2,T);V.UV3=FMath::Lerp(A.UV3,B.UV3,T);
        V.Color=FMath::Lerp(FLinearColor(A.Color),FLinearColor(B.Color),float(T)).ToFColor(true);
        return V;
    }
    void Split(const FPolygon& Input,int32 Axis,double Line,FPolygon& Low,FPolygon& High)
    {
        Low.Reset();High.Reset();if(Input.Num()<3)return;
        for(int32 I=0;I<Input.Num();++I)
        {
            const auto& A=Input[I];const auto& B=Input[(I+1)%Input.Num()];
            const double DA=A.Position[Axis]-Line,DB=B.Position[Axis]-Line;
            if(DA<=Epsilon)Low.Add(A);
            if(DA>=-Epsilon)High.Add(A);
            if((DA<-Epsilon&&DB>Epsilon)||(DA>Epsilon&&DB<-Epsilon))
            {
                auto V=Interpolate(A,B,DA/(DA-DB));V.Position[Axis]=Line;
                Low.Add(V);High.Add(V);
            }
        }
    }
    FPolygon Extract(const FPolygon& Input,const FFarm& Farm,TArray<FPolygon>& Outside)
    {
        FPolygon Current=Input,Low,High;
        for(int32 Axis=0;Axis<2;++Axis)
        {
            Split(Current,Axis,Farm.Center[Axis]-Farm.Half(),Low,High);
            if(Low.Num()>=3)Outside.Add(MoveTemp(Low));Current=MoveTemp(High);
            Split(Current,Axis,Farm.Center[Axis]+Farm.Half(),Low,High);
            if(High.Num()>=3)Outside.Add(MoveTemp(High));Current=MoveTemp(Low);
        }
        return Current;
    }
    double AreaXY(const FVector& A,const FVector& B,const FVector& C)
    {return FMath::Abs((B.X-A.X)*(C.Y-A.Y)-(B.Y-A.Y)*(C.X-A.X))*.5;}
    double Emit(FProcMeshSection& Section,const FPolygon& Poly,const FFarm* Farm=nullptr)
    {
        double Area=0;
        for(int32 I=1;I+1<Poly.Num();++I)
        {
            FProcMeshVertex V[3]={Poly[0],Poly[I],Poly[I+1]};
            const double A=AreaXY(V[0].Position,V[1].Position,V[2].Position);if(A<1.e-12)continue;
            Area+=A;
            if(Farm)
            {
                for(auto& P:V)P.Position.Z=Farm->Height(P.Position);
                FVector N=FVector::CrossProduct(V[1].Position-V[0].Position,V[2].Position-V[0].Position).GetSafeNormal();
                if(N.Z<0)N=-N;
                for(auto& P:V)
                {
                    // 原框上的法线保持原插值；内部细分面按新坡面更新，避免沿用旧陡坡法线。-codex，2026-10-30
                    const double Q=FMath::Max(FMath::Abs(P.Position.X-Farm->Center.X),FMath::Abs(P.Position.Y-Farm->Center.Y))/Farm->Half();
                    if(Q<1.-Epsilon)P.Normal=N;
                    const FVector T=(FVector::XAxisVector-P.Normal*P.Normal.X).GetSafeNormal();
                    P.Tangent=FProcMeshTangent(T,false);
                }
            }
            for(const auto& P:V)
            {
                Section.ProcIndexBuffer.Add(Section.ProcVertexBuffer.Add(P));Section.SectionLocalBox+=P.Position;
            }
        }
        return Area;
    }
    double EmitPatch(FProcMeshSection& Section,const FPolygon& Input,const FFarm& Farm)
    {
        // 用同一世界坐标网格裁分原三角形，新增边界UV均由原边插值；不是在500m稀疏点上硬拉平。-codex，2026-10-30
        TArray<double> Lines;
        for(int32 I=1;I<16;++I)Lines.Add(-1.+I/8.);
        Lines.Add(-.72);Lines.Add(.72);Lines.Sort();
        TArray<FPolygon> Strips;FPolygon Remaining=Input;
        for(double X:Lines)
        {
            FPolygon L,H;Split(Remaining,0,Farm.Center.X+X*Farm.Half(),L,H);
            if(L.Num()>=3)Strips.Add(MoveTemp(L));Remaining=MoveTemp(H);
        }
        if(Remaining.Num()>=3)Strips.Add(MoveTemp(Remaining));
        double Area=0;
        for(auto& Strip:Strips)
        {
            Remaining=MoveTemp(Strip);
            for(double Y:Lines)
            {
                FPolygon L,H;Split(Remaining,1,Farm.Center.Y+Y*Farm.Half(),L,H);
                Area+=Emit(Section,L,&Farm);Remaining=MoveTemp(H);
            }
            Area+=Emit(Section,Remaining,&Farm);
        }
        return Area;
    }
    bool Sample(const FProcMeshSection& Section,FVector& Point)
    {
        for(int32 I=0;I+2<Section.ProcIndexBuffer.Num();I+=3)
        {
            const FVector A=Section.ProcVertexBuffer[Section.ProcIndexBuffer[I]].Position;
            const FVector B=Section.ProcVertexBuffer[Section.ProcIndexBuffer[I+1]].Position;
            const FVector C=Section.ProcVertexBuffer[Section.ProcIndexBuffer[I+2]].Position;
            if(Point.X<FMath::Min3(A.X,B.X,C.X)-Epsilon||Point.X>FMath::Max3(A.X,B.X,C.X)+Epsilon||
               Point.Y<FMath::Min3(A.Y,B.Y,C.Y)-Epsilon||Point.Y>FMath::Max3(A.Y,B.Y,C.Y)+Epsilon)continue;
            const FVector U=B-A,V=C-A,Q=Point-A;const double D=U.X*V.Y-U.Y*V.X;
            if(FMath::Abs(D)<1.e-12)continue;
            const double X=(Q.X*V.Y-Q.Y*V.X)/D,Y=(U.X*Q.Y-U.Y*Q.X)/D;
            if(X>=-Epsilon&&Y>=-Epsilon&&X+Y<=1.+Epsilon)
            {Point.Z=A.Z+X*U.Z+Y*V.Z;return true;}
        }
        return false;
    }
    bool Valid(const FProcMeshSection& S)
    {
        if(S.ProcIndexBuffer.Num()%3)return false;
        for(const auto& V:S.ProcVertexBuffer)
            if(V.Position.ContainsNaN()||V.Normal.ContainsNaN()||V.UV0.ContainsNaN())return false;
        for(int32 I=0;I<S.ProcIndexBuffer.Num();I+=3)
        {
            const uint32 A=S.ProcIndexBuffer[I],B=S.ProcIndexBuffer[I+1],C=S.ProcIndexBuffer[I+2];
            if(!S.ProcVertexBuffer.IsValidIndex(A)||!S.ProcVertexBuffer.IsValidIndex(B)||!S.ProcVertexBuffer.IsValidIndex(C))return false;
            if(FVector::CrossProduct(S.ProcVertexBuffer[B].Position-S.ProcVertexBuffer[A].Position,
                S.ProcVertexBuffer[C].Position-S.ProcVertexBuffer[A].Position).SizeSquared()<1.e-24)return false;
        }
        return true;
    }
    struct FSurface
    {
        TArray<FVector> P,N;TArray<int32> I;TArray<FVector2D> UV;TArray<FLinearColor> C;TArray<FProcMeshTangent> T;
        void Quad(FVector A,FVector B,FVector D,FVector E,const FLinearColor& Color=FLinearColor::White)
        {
            const int32 K=P.Num();const FVector Normal=FVector::CrossProduct(B-A,D-A).GetSafeNormal();
            const FVector Tangent=(B-A).GetSafeNormal();
            P.Append({A,B,D,E});UV.Append({FVector2D(0,0),FVector2D(1,0),FVector2D(1,1),FVector2D(0,1)});
            for(int32 J=0;J<4;++J){N.Add(Normal);C.Add(Color);T.Add(FProcMeshTangent(Tangent,false));}
            // UE CalculateTangentsForMesh按(P1-P2)叉(P0-P2)取正面法线；反序索引与上方外法线一致。-codex，2026-10-30
            I.Append({K,K+2,K+1,K,K+3,K+2});
        }
        void Rect(double X0,double X1,double Y0,double Y1,double Z,FLinearColor Color=FLinearColor::White)
        {Quad(FVector(X0,Y0,Z),FVector(X1,Y0,Z),FVector(X1,Y1,Z),FVector(X0,Y1,Z),Color);}
        void SetFieldUV(double Half)
        {
            // 窄土埂不再各自重复一整张纹理；所有土面/作业路按同一场址尺度连续采样。-codex，2026-10-30
            for(int32 J=0;J<P.Num();++J)
            {
                UV[J]=FVector2D(P[J].X/(2.*Half)+.5,P[J].Y/(2.*Half)+.5);
                const FVector U=(FVector::XAxisVector-N[J]*N[J].X).GetSafeNormal();
                const FVector V=FVector::YAxisVector-N[J]*N[J].Y;
                T[J]=FProcMeshTangent(U,FVector::DotProduct(FVector::CrossProduct(N[J],U),V)<0.);
            }
        }
        bool Submit(AActor* Owner,UMaterialInterface* Material,const TCHAR* Name) const
        {
            auto* Mesh=NewObject<UProceduralMeshComponent>(Owner,FName(Name));if(!Mesh)return false;
            Owner->AddInstanceComponent(Mesh);Mesh->SetupAttachment(Owner->GetRootComponent());Mesh->RegisterComponent();
            Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Mesh->CreateMeshSection_LinearColor(0,P,I,N,UV,C,T,false,false);Mesh->SetMaterial(0,Material);return true;
        }
    };
    bool BuildFarmActor(AActor* Actor,FFarm& Farm,UMaterialInterface* WaterMat,UMaterialInterface* BankMat)
    {
        const double H=Farm.Half(),Top=Farm.Radius*.008,Bottom=-Farm.Radius*.022+Farm.Radius*.00025;
        FSurface Water,Bank,Road;
        // 一长塘、两短塘共用作业埂，面积只用于构图；不按台账推断实际塘界。-codex，2026-10-30
        const double X[]={-.62,-.55,-.08,.02,.55,.62},Y[]={-.58,-.50,-.09,.01,.50,.58};
        auto TopPoint=[&](double PX,double PY)
        {
            if(FMath::IsNearlyEqual(FMath::Abs(PX),.62)&&FMath::Abs(PY)>.50)
                PX=FMath::Sign(PX)*(.62-.05*(FMath::Abs(PY)-.50)/.08);
            return FVector(PX*H,PY*H,Top);
        };
        for(int32 IX=0;IX<5;++IX)for(int32 IY=0;IY<5;++IY)
        {
            const bool Pond=(IX==1&&IY>=1&&IY<=3)||(IX==3&&(IY==1||IY==3));
            if(!Pond)Bank.Quad(TopPoint(X[IX],Y[IY]),TopPoint(X[IX+1],Y[IY]),
                TopPoint(X[IX+1],Y[IY+1]),TopPoint(X[IX],Y[IY+1]));
        }
        const FVector4 Ponds[]={FVector4(-.55,-.08,-.50,.50),FVector4(.02,.55,-.50,-.09),FVector4(.02,.55,.01,.50)};
        for(int32 Index=0;Index<3;++Index)
        {
            const auto& P=Ponds[Index];
            const double X0=P.X*H,X1=P.Y*H,Y0=P.Z*H,Y1=P.W*H,Inset=.020*H,Low=-Farm.Radius*.004;
            Water.Rect(X0+Inset,X1-Inset,Y0+Inset,Y1-Inset,0,
                Index==0?FLinearColor(.93f,1.f,.97f):Index==1?FLinearColor(.98f,.95f,.91f):FLinearColor(.91f,.97f,1.f));
            // 显示锚点从本次真实水面矩形派生，绝不改变场址中心、模型半径或裁地。-codex，2026-11-02
            if(Index==0)
            {
                Farm.WaterAnchor=Farm.Center+FVector((X0+X1)*.5,(Y0+Y1)*.5,0);
                Farm.WaterHalfExtent=FVector2D((X1-X0)*.5-Inset,(Y1-Y0)*.5-Inset);
            }
            const FVector Outer[]={FVector(X0,Y0,Top),FVector(X1,Y0,Top),FVector(X1,Y1,Top),FVector(X0,Y1,Top)};
            const FVector Inner[]={FVector(X0+Inset,Y0+Inset,Low),FVector(X1-Inset,Y0+Inset,Low),FVector(X1-Inset,Y1-Inset,Low),FVector(X0+Inset,Y1-Inset,Low)};
            for(int32 K=0;K<4;++K)Bank.Quad(Outer[K],Outer[(K+1)%4],Inner[(K+1)%4],Inner[K]);
        }
        const FVector2D Perimeter[]={FVector2D(-.57,-.58),FVector2D(.57,-.58),FVector2D(.62,-.50),FVector2D(.62,.50),
            FVector2D(.57,.58),FVector2D(-.57,.58),FVector2D(-.62,.50),FVector2D(-.62,-.50)};
        for(int32 K=0;K<8;++K)
        {
            const auto& A=Perimeter[K];const auto& B=Perimeter[(K+1)%8];
            const FVector InnerA(A.X*H,A.Y*H,Top),InnerB(B.X*H,B.Y*H,Top);
            const FVector OuterA(A.X*1.11*H,A.Y*1.11*H,Bottom),OuterB(B.X*1.11*H,B.Y*1.11*H,Bottom);
            const int32 First=Bank.P.Num();Bank.Quad(OuterA,OuterB,InnerB,InnerA);
            // alpha仅是土肩到原遥感色的混合权重，不是透明度；外缘0、内侧1，土肩下方地面已真实削平。-codex，2026-10-30
            Bank.C[First].A=0;Bank.C[First+1].A=0;
        }
        // 短路压低且偏离对称轴，避免中央高亮十字让渔场看成UI图标。-codex，2026-10-30
        Road.Rect(-.051*H,-.009*H,-.55*H,.55*H,Top+Farm.Radius*.001,FLinearColor(.80f,.76f,.65f));
        Bank.SetFieldUV(H);Road.SetFieldUV(H);
        return Bank.Submit(Actor,BankMat,TEXT("FarmBank"))&&Water.Submit(Actor,WaterMat,TEXT("FarmWater"))&&Road.Submit(Actor,BankMat,TEXT("FarmWorkPath"));
    }
}

// 所有变体只缓存最大示意范围内的原始三角；静态城市面不随选中重烹。-codex，2026-11-02
namespace SurveyFarmGeometry
{
    // 选中展示沿用普通渔场的常用尺寸，取消此前32的大塘特写。密集近邻仍按已批准方式暂隐。-codex，2026-11-03
    constexpr float SelectedRadius=8.f;
    constexpr uint64 CacheBudget=256ull*1024*1024;
    struct FVariant
    {
        FString Selected;
        TArray<FFarm> Farms;
        TArray<uint8> Displayed;
        TArray<FProcMeshSection> Sections;
        TArray<TWeakObjectPtr<UProceduralMeshComponent>> LocalMeshes;
    };
    struct FVertexKey
    {
        FProcMeshVertex V;
        bool operator==(const FVertexKey& Other) const
        {
            const auto& B=Other.V;
            return V.Position==B.Position&&V.Normal==B.Normal&&V.Color==B.Color&&
                V.Tangent.TangentX==B.Tangent.TangentX&&V.Tangent.bFlipTangentY==B.Tangent.bFlipTangentY&&
                V.UV0==B.UV0&&V.UV1==B.UV1&&V.UV2==B.UV2&&V.UV3==B.UV3;
        }
        friend uint32 GetTypeHash(const FVertexKey& Key){return GetTypeHash(Key.V.Position);}
    };
    bool SameGeometry(const FProcMeshSection& A,const FProcMeshSection& B)
    {
        if(A.ProcVertexBuffer.Num()!=B.ProcVertexBuffer.Num()||A.ProcIndexBuffer!=B.ProcIndexBuffer)return false;
        for(int32 I=0;I<A.ProcVertexBuffer.Num();++I)
            if(!(FVertexKey{A.ProcVertexBuffer[I]}==FVertexKey{B.ProcVertexBuffer[I]}))return false;
        return true;
    }
    void Compact(FProcMeshSection& Section)
    {
        // 仅完整顶点属性相等时合并，不能用位置焊接破坏坡面硬法线或UV接缝。-codex，2026-11-02
        auto Original=MoveTemp(Section.ProcVertexBuffer);
        TMap<FVertexKey,uint32> Vertices;
        for(auto& Index:Section.ProcIndexBuffer)
        {
            const FVertexKey Key{Original[Index]};
            if(const uint32* Existing=Vertices.Find(Key))Index=*Existing;
            else
            {
                Index=Section.ProcVertexBuffer.Add(Key.V);Vertices.Add(Key,Index);
            }
        }
        Section.ProcVertexBuffer.Shrink();Section.ProcIndexBuffer.Shrink();
    }
    bool Intersects(const FFarm& A,const FFarm& B)
    {
        const double H=A.Half()+B.Half();
        return FMath::Abs(A.Center.X-B.Center.X)<H&&FMath::Abs(A.Center.Y-B.Center.Y)<H;
    }
    bool Grade(FFarm& Farm,const TArray<FTerrain>& Terrains,FString& Error)
    {
        Farm.Center.Z=Farm.OriginalCenterZ;
        TArray<double> Heights;
        for(int32 X=0;X<=8;++X)for(int32 Y=0;Y<=8;++Y)
        {
            FVector P=Farm.Center+FVector((-1.+X*.25)*Farm.Half(),(-1.+Y*.25)*Farm.Half(),0);
            for(const auto& T:Terrains)if(Sample(T.LocalOriginal,P)){Heights.Add(P.Z);break;}
        }
        if(Heights.Num()<9){Error=FString::Printf(TEXT("Insufficient original terrain samples: %s"),*Farm.Id);return false;}
        Heights.Sort();
        Farm.Center.Z=FMath::Max(FMath::Min(Farm.OriginalCenterZ,Heights[(Heights.Num()-1)/4]),Farm.OriginalCenterZ-Farm.Radius*.35);
        return true;
    }
    bool Carve(const FProcMeshSection& Source,FVariant& Variant,FProcMeshSection& Output,FString& Error)
    {
        // 未裁到的局部原面共享索引，避免每个变体把普通地面顶点展开三倍。-codex，2026-11-02
        Output.ProcVertexBuffer=Source.ProcVertexBuffer;Output.SectionLocalBox=Source.SectionLocalBox;
        for(int32 I=0;I<Source.ProcIndexBuffer.Num();I+=3)
        {
            const auto& A=Source.ProcVertexBuffer[Source.ProcIndexBuffer[I]];
            const auto& B=Source.ProcVertexBuffer[Source.ProcIndexBuffer[I+1]];
            const auto& C=Source.ProcVertexBuffer[Source.ProcIndexBuffer[I+2]];
            TArray<int32> Candidates;
            for(int32 J=0;J<Variant.Farms.Num();++J)
                if(Variant.Displayed[J]&&Variant.Farms[J].Overlaps(A.Position,B.Position,C.Position))Candidates.Add(J);
            if(Candidates.IsEmpty())
            {Output.ProcIndexBuffer.Append({Source.ProcIndexBuffer[I],Source.ProcIndexBuffer[I+1],Source.ProcIndexBuffer[I+2]});continue;}
            TArray<FPolygon> Pieces;Pieces.Add(FPolygon{A,B,C});double Area=0;
            for(int32 J:Candidates)
            {
                auto& Farm=Variant.Farms[J];TArray<FPolygon> Outside;
                for(const auto& Piece:Pieces)
                {
                    const FPolygon Inside=Extract(Piece,Farm,Outside);
                    const double PatchArea=EmitPatch(Output,Inside,Farm);
                    if(PatchArea>1.e-12){++Farm.ChangedTriangles;Area+=PatchArea;}
                }
                Pieces=MoveTemp(Outside);
            }
            for(const auto& Piece:Pieces)Area+=Emit(Output,Piece);
            const double OriginalArea=AreaXY(A.Position,B.Position,C.Position);
            if(FMath::Abs(Area-OriginalArea)>FMath::Max(1.e-7,OriginalArea*1.e-7))
            {Error=TEXT("Survey local terrain area conservation failed.");return false;}
        }
        Output.bEnableCollision=true;Compact(Output);
        if(!Valid(Output)){Error=TEXT("Survey local terrain contains invalid geometry.");return false;}
        return true;
    }
}

class FSurveyFarmScene
{
public:
    FString Version;
    TArray<SurveyFarmGeometry::FTerrain> Terrains;
    TArray<SurveyFarmGeometry::FVariant> Variants;
    TArray<TWeakObjectPtr<AActor>> NormalActors,LargeActors;
    TArray<TWeakObjectPtr<UProceduralMeshComponent>> PreparedMeshes;
    int32 Active=0;
    uint32 Revision=1;
    bool Applied=false,Visible=false;
    ~FSurveyFarmScene()
    {
        // 提交前失败只销毁待用实体；提交后的回收恢复原城市节，保留其他节。-codex，2026-11-02
        if(Applied)for(auto& T:Terrains)if(T.Mesh.IsValid())T.Mesh->SetProcMeshSection(0,T.Original);
        for(auto& Mesh:PreparedMeshes)if(Mesh.IsValid())Mesh->DestroyComponent();
        for(auto& A:NormalActors)if(A.IsValid())A->Destroy();
        for(auto& A:LargeActors)if(A.IsValid())A->Destroy();
    }
    void ApplyVisibility()
    {
        const auto& V=Variants[Active];
        for(int32 I=0;I<V.Farms.Num();++I)
        {
            const bool Selected=V.Farms[I].Id==V.Selected;
            if(NormalActors[I].IsValid())NormalActors[I]->SetActorHiddenInGame(!Visible||!V.Displayed[I]||Selected);
            if(LargeActors[I].IsValid())LargeActors[I]->SetActorHiddenInGame(!Visible||!Selected);
        }
    }
};

bool AFujianMap::BuildSurveyFarms(const Xinluo::FDataset& Dataset,FString& BuildError)
{
    using namespace SurveyFarmGeometry;
    BuildError.Empty();
    if(SurveyFarmScene)
    {
        if(SurveyFarmScene->Version==Dataset.Version)return true;
        BuildError=TEXT("Survey dataset changed; recreate map before rebuilding terrain.");return false;
    }
    const double Started=FPlatformTime::Seconds();
    auto* Water=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Terrain/M_SurveyWater.M_SurveyWater"));
    auto* Bank=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Terrain/M_SurveyBank.M_SurveyBank"));
    if(!Water||!Bank||!GetWorld()){BuildError=TEXT("Survey farm material or world missing.");return false;}
    auto Scene=MakeShared<FSurveyFarmScene>();Scene->Version=Dataset.Version;
    TArray<FFarm> Normal,Large;
    for(const auto& S:Dataset.Subjects)if(S.HasCoordinate)
    {
        FFarm F;F.Id=S.Id;F.Center=FVector((S.Latitude-26.05)*1000.,(S.Longitude-118.2)*1000.*FMath::Cos(FMath::DegreesToRadians(26.05)),0);
        bool Found=false;
        for(const auto& City:Cities)if(City.Terrain.IsValid())
        {
            if(!City.Terrain->GetComponentTransform().Equals(FTransform::Identity))
            {BuildError=TEXT("Survey terrain requires identity-transform geometry.");return false;}
            if(const auto* Section=City.Terrain->GetProcMeshSection(0))if(Sample(*Section,F.Center)){Found=true;break;}
        }
        if(!Found){BuildError=FString::Printf(TEXT("Survey farm has no terrain intersection: %s"),*F.Id);return false;}
        F.OriginalCenterZ=F.Center.Z;Normal.Add(F);
    }
    if(Normal.IsEmpty()){BuildError=TEXT("Survey farms require coordinate-bearing subjects.");return false;}
    for(auto& F:Normal)
    {
        double Nearest=DBL_MAX;
        for(const auto& Other:Normal)if(Other.Id!=F.Id)Nearest=FMath::Min(Nearest,FVector::Dist2D(F.Center,Other.Center));
        F.Radius=FMath::Min(8.f,float(.4*Nearest));
        if(!FMath::IsFinite(F.Radius)||F.Radius<.01f){BuildError=TEXT("Coincident survey anchors.");return false;}
        FFarm Big=F;Big.Radius=SelectedRadius;Large.Add(Big);
    }
    int64 OriginalTriangles=0,LocalTriangles=0;
    // 每原三角只归属一次局部并集；不以空间分组重复收取跨组大三角。-codex，2026-11-02
    for(const auto& City:Cities)if(City.Terrain.IsValid())
    {
        const auto* Source=City.Terrain->GetProcMeshSection(0);if(!Source)continue;
        bool Relevant=false;
        for(const auto& F:Large)if(Source->SectionLocalBox.Intersect(FBox(F.Center-FVector(F.Half(),F.Half(),1000),F.Center+FVector(F.Half(),F.Half(),1000)))){Relevant=true;break;}
        if(!Relevant)continue;
        FTerrain T;T.Mesh=City.Terrain;T.Original=*Source;T.Static=*Source;T.Static.ProcIndexBuffer.Reset();
        TMap<uint32,uint32> LocalVertices;
        for(int32 I=0;I<Source->ProcIndexBuffer.Num();I+=3)
        {
            const uint32 IA=Source->ProcIndexBuffer[I],IB=Source->ProcIndexBuffer[I+1],IC=Source->ProcIndexBuffer[I+2];
            const auto& A=Source->ProcVertexBuffer[IA];const auto& B=Source->ProcVertexBuffer[IB];const auto& C=Source->ProcVertexBuffer[IC];
            bool Local=false;
            for(const auto& F:Large)if(F.Overlaps(A.Position,B.Position,C.Position)){Local=true;break;}
            if(Local)for(uint32 Index:{IA,IB,IC})
            {
                const uint32* Existing=LocalVertices.Find(Index);
                if(Existing)T.LocalOriginal.ProcIndexBuffer.Add(*Existing);
                else
                {
                    const auto& Vertex=Source->ProcVertexBuffer[Index];
                    const uint32 NewIndex=T.LocalOriginal.ProcVertexBuffer.Add(Vertex);
                    LocalVertices.Add(Index,NewIndex);T.LocalOriginal.ProcIndexBuffer.Add(NewIndex);
                    T.LocalOriginal.SectionLocalBox+=Vertex.Position;
                }
            }
            else T.Static.ProcIndexBuffer.Append({IA,IB,IC});
        }
        if(T.LocalOriginal.ProcIndexBuffer.IsEmpty())continue;
        OriginalTriangles+=Source->ProcIndexBuffer.Num()/3;LocalTriangles+=T.LocalOriginal.ProcIndexBuffer.Num()/3;
        UE_LOG(LogTemp,Display,TEXT("SurveyFarmLocal city=%d originalTriangles=%d localTriangles=%d existingSections=%d"),City.Code,Source->ProcIndexBuffer.Num()/3,T.LocalOriginal.ProcIndexBuffer.Num()/3,City.Terrain->GetNumSections());
        Scene->Terrains.Add(MoveTemp(T));
    }
    for(auto& F:Normal)if(!Grade(F,Scene->Terrains,BuildError))return false;
    for(auto& F:Large)if(!Grade(F,Scene->Terrains,BuildError))return false;
    auto SpawnFarm=[&](FFarm& Farm,TArray<TWeakObjectPtr<AActor>>& Actors)
    {
        auto* Actor=GetWorld()->SpawnActor<AActor>();if(!Actor)return false;
        Actors.Add(Actor);Actor->SetActorHiddenInGame(true);Actor->SetOwner(this);
        auto* Root=NewObject<USceneComponent>(Actor);if(!Root)return false;
        Actor->AddInstanceComponent(Root);Actor->SetRootComponent(Root);Root->RegisterComponent();Actor->SetActorLocation(Farm.Center);
        return BuildFarmActor(Actor,Farm,Water,Bank);
    };
    for(auto& F:Normal)if(!SpawnFarm(F,Scene->NormalActors)){BuildError=TEXT("Survey normal model allocation failed.");return false;}
    for(auto& F:Large)if(!SpawnFarm(F,Scene->LargeActors)){BuildError=TEXT("Survey enlarged model allocation failed.");return false;}
    uint64 CacheBytes=0;int64 CachedTriangles=0;
    for(int32 Selected=INDEX_NONE;Selected<Normal.Num();++Selected)
    {
        SurveyFarmGeometry::FVariant V;V.Farms=Normal;V.Displayed.Init(1,Normal.Num());
        if(Selected!=INDEX_NONE)
        {
            V.Selected=Normal[Selected].Id;V.Farms[Selected]=Large[Selected];
            for(int32 J=0;J<Normal.Num();++J)if(J!=Selected&&Intersects(V.Farms[Selected],Normal[J]))V.Displayed[J]=0;
        }
        // 裁地前明确证明活动矩形互斥，不能依赖裁分先后掩盖重叠。-codex，2026-11-02
        for(int32 A=0;A<V.Farms.Num();++A)if(V.Displayed[A])for(int32 B=A+1;B<V.Farms.Num();++B)
            if(V.Displayed[B]&&Intersects(V.Farms[A],V.Farms[B])){BuildError=TEXT("Survey active grading rectangles overlap.");return false;}
        for(auto& F:V.Farms)F.ChangedTriangles=0;
        for(const auto& T:Scene->Terrains)
        {
            FProcMeshSection Section;
            if(!Carve(T.LocalOriginal,V,Section,BuildError))return false;
            CacheBytes+=Section.ProcVertexBuffer.GetAllocatedSize()+Section.ProcIndexBuffer.GetAllocatedSize();
            CachedTriangles+=Section.ProcIndexBuffer.Num()/3;
            if(CacheBytes>CacheBudget){BuildError=TEXT("Survey local variant cache exceeds 256 MiB budget.");return false;}
            V.Sections.Add(MoveTemp(Section));
        }
        for(int32 J=0;J<V.Farms.Num();++J)if(V.Displayed[J]&&!V.Farms[J].ChangedTriangles)
        {BuildError=FString::Printf(TEXT("Survey active farm patch not carved: %s"),*V.Farms[J].Id);return false;}
        Scene->Variants.Add(MoveTemp(V));
    }
    uint64 PreparedGeometryBytes=0;
    // 各不同几何在初始化时完成烹饪与渲染准备；相同城市变体共用组件。-codex，2026-11-02
    for(int32 VariantIndex=0;VariantIndex<Scene->Variants.Num();++VariantIndex)
    {
        auto& V=Scene->Variants[VariantIndex];
        for(int32 I=0;I<Scene->Terrains.Num();++I)
        {
            auto& T=Scene->Terrains[I];auto& Section=V.Sections[I];
            UProceduralMeshComponent* Mesh=nullptr;
            for(int32 Previous=0;Previous<VariantIndex&&!Mesh;++Previous)
            {
                auto* Candidate=Scene->Variants[Previous].LocalMeshes[I].Get();
                const auto* Geometry=Candidate?Candidate->GetProcMeshSection(0):nullptr;
                if(Geometry&&SameGeometry(Section,*Geometry))Mesh=Candidate;
            }
            if(!Mesh)
            {
                Mesh=NewObject<UProceduralMeshComponent>(this);
                if(!Mesh){BuildError=TEXT("Survey prepared terrain component allocation failed.");return false;}
                Scene->PreparedMeshes.Add(Mesh);AddInstanceComponent(Mesh);Mesh->SetupAttachment(T.Mesh.Get());
                // 非活动组件仍保留已建立物理状态，仅过滤碰撞；不在选择时创建body。-codex，2026-11-02
                Mesh->SetCollisionResponseToAllChannels(ECR_Ignore);
                Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
                Mesh->bUseComplexAsSimpleCollision=true;Mesh->bUseAsyncCooking=false;
                Mesh->SetGenerateOverlapEvents(false);Mesh->RegisterComponent();Mesh->SetMaterial(0,T.Mesh->GetMaterial(0));
                Section.bSectionVisible=false;Mesh->SetProcMeshSection(0,Section);
                const auto* Body=Mesh->GetBodySetup();
                if(!Body||!Body->bCreatedPhysicsMeshes||Body->bFailedToCreatePhysicsMeshes||!Mesh->IsPhysicsStateCreated())
                {BuildError=TEXT("Survey prepared terrain physics initialization failed.");return false;}
                const auto* Geometry=Mesh->GetProcMeshSection(0);
                PreparedGeometryBytes+=Geometry->ProcVertexBuffer.GetAllocatedSize()+Geometry->ProcIndexBuffer.GetAllocatedSize();
            }
            V.LocalMeshes.Add(Mesh);
            // 组件接管唯一几何缓存，立即释放构造数组，避免常驻双份顶点。-codex，2026-11-02
            Section=FProcMeshSection();
        }
        V.Sections.Empty();
    }
    // 所有变体物理状态就绪才提交；任何准备失败仍保留原城市面。-codex，2026-11-02
    for(int32 I=0;I<Scene->Terrains.Num();++I)
    {
        auto& T=Scene->Terrains[I];T.Mesh->SetProcMeshSection(0,T.Static);T.Static=FProcMeshSection();
        T.LocalMesh=Scene->Variants[0].LocalMeshes[I];
        T.LocalMesh->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
        T.LocalMesh->SetMeshSectionVisible(0,true);
    }
    Scene->Applied=true;
    for(const auto& F:Normal)
        UE_LOG(LogTemp,Display,TEXT("SurveyFarm id=%s xy=%.8f,%.8f center=%s radius=%.6f cutDepth=%.6f schematic=1"),*F.Id,F.Center.X,F.Center.Y,*F.Center.ToString(),F.Radius,F.OriginalCenterZ-F.Center.Z);
    UE_LOG(LogTemp,Display,TEXT("SurveyFarms built sites=%d localComponents=%d preparedComponents=%d variants=%d originalTriangles=%lld localTriangles=%lld cachedTriangles=%lld cacheBytes=%llu geometryBytes=%llu elapsedMs=%.3f originalDEM=unchanged"),Normal.Num(),Scene->Terrains.Num(),Scene->PreparedMeshes.Num(),Scene->Variants.Num(),OriginalTriangles,LocalTriangles,CachedTriangles,CacheBytes,PreparedGeometryBytes,(FPlatformTime::Seconds()-Started)*1000.);
    SurveyFarmScene=MoveTemp(Scene);return true;
}

bool AFujianMap::SetSurveyFarmSelection(const FString& Id,FString& SelectionError)
{
    SelectionError.Empty();if(!SurveyFarmScene){SelectionError=TEXT("Survey farm scene unavailable.");return false;}
    auto& S=*SurveyFarmScene;
    const int32 Next=S.Variants.IndexOfByPredicate([&](const SurveyFarmGeometry::FVariant& V){return V.Selected==Id;});
    if(Next==INDEX_NONE){SelectionError=TEXT("Survey selection has no prepared geometry.");return false;}
    if(Next==S.Active)return true;
    // 预检旧/新完整资源，切换只动显隐与过滤，禁止重新提交几何或烹饪。-codex，2026-11-02
    for(const auto& T:S.Terrains)if(!T.Mesh.IsValid()||!T.LocalMesh.IsValid()){SelectionError=TEXT("Survey terrain component expired.");return false;}
    for(const auto& A:S.NormalActors)if(!A.IsValid()){SelectionError=TEXT("Survey normal actor expired.");return false;}
    for(const auto& A:S.LargeActors)if(!A.IsValid()){SelectionError=TEXT("Survey enlarged actor expired.");return false;}
    const double Started=FPlatformTime::Seconds();auto& V=S.Variants[Next];
    if(V.LocalMeshes.Num()!=S.Terrains.Num()){SelectionError=TEXT("Survey prepared terrain count mismatch.");return false;}
    for(const auto& Mesh:V.LocalMeshes)
        if(!Mesh.IsValid()||!Mesh->IsPhysicsStateCreated()){SelectionError=TEXT("Survey prepared terrain unavailable.");return false;}
    for(int32 I=0;I<S.Terrains.Num();++I)
    {
        auto& T=S.Terrains[I];auto* NextMesh=V.LocalMeshes[I].Get();
        if(T.LocalMesh.Get()==NextMesh)continue;
        T.LocalMesh->SetMeshSectionVisible(0,false);
        T.LocalMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
        NextMesh->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
        NextMesh->SetMeshSectionVisible(0,true);T.LocalMesh=NextMesh;
    }
    S.Active=Next;++S.Revision;S.ApplyVisibility();
    FString Hidden;
    for(int32 I=0;I<V.Farms.Num();++I)if(!V.Displayed[I]){if(!Hidden.IsEmpty())Hidden+=TEXT(",");Hidden+=V.Farms[I].Id;}
    UE_LOG(LogTemp,Display,TEXT("SurveyFarmSelection id=%s revision=%u radius=%.1f hidden=%s elapsedMs=%.3f"),Id.IsEmpty()?TEXT("city"):*Id,S.Revision,Id.IsEmpty()?0.f:SurveyFarmGeometry::SelectedRadius,*Hidden,(FPlatformTime::Seconds()-Started)*1000.);
    return true;
}
uint32 AFujianMap::SurveyFarmGeometryRevision() const
{return SurveyFarmScene?SurveyFarmScene->Revision:0;}
bool AFujianMap::IsSurveyFarmDisplayed(const FString& Id) const
{
    if(!SurveyFarmScene)return false;
    const auto& V=SurveyFarmScene->Variants[SurveyFarmScene->Active];
    for(int32 I=0;I<V.Farms.Num();++I)if(V.Farms[I].Id==Id)return V.Displayed[I]!=0;
    return false;
}
bool AFujianMap::TraceSurveyFarmTerrain(FHitResult& Hit,const FVector& Start,const FVector& End) const
{
    bool Found=false;double Distance=DBL_MAX;
    if(SurveyFarmScene)for(const auto& T:SurveyFarmScene->Terrains)if(T.LocalMesh.IsValid())
    {
        FHitResult Candidate;
        if(T.LocalMesh->LineTraceComponent(Candidate,Start,End,FCollisionQueryParams(SCENE_QUERY_STAT(SurveyLocalTerrain),true))&&Candidate.Distance<Distance)
        {Hit=Candidate;Distance=Candidate.Distance;Found=true;}
    }
    return Found;
}
void AFujianMap::GetSurveyFarmSites(TArray<FString>& Ids,TArray<FVector>& Centers,TArray<float>& Radii) const
{
    Ids.Reset();Centers.Reset();Radii.Reset();if(!SurveyFarmScene)return;
    for(const auto& F:SurveyFarmScene->Variants[SurveyFarmScene->Active].Farms){Ids.Add(F.Id);Centers.Add(F.Center);Radii.Add(F.Radius);}
}
bool AFujianMap::GetSurveyFarmFocus(const FString& Id,FVector& Center,float& Radius) const
{
    if(SurveyFarmScene)for(const auto& F:SurveyFarmScene->Variants[SurveyFarmScene->Active].Farms)
        if(F.Id==Id){Center=F.Center;Radius=F.Radius;return true;}
    return false;
}
void AFujianMap::SetSurveyFarmsVisible(bool Visible)
{if(SurveyFarmScene){SurveyFarmScene->Visible=Visible;SurveyFarmScene->ApplyVisibility();}}
bool AFujianMap::GetSurveyFarmWaterAnchor(const FString& Id,FVector& Anchor,FVector2D& HalfExtent) const
{
    if(SurveyFarmScene)for(const auto& F:SurveyFarmScene->Variants[SurveyFarmScene->Active].Farms)
        if(F.Id==Id){Anchor=F.WaterAnchor;HalfExtent=F.WaterHalfExtent;return HalfExtent.X>0&&HalfExtent.Y>0;}
    return false;
}
