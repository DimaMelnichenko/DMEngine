#include "AnimationPose.h"

using namespace DirectX;

namespace GS
{

JointTransform blendJoint( const JointTransform& a, const JointTransform& b, float weight )
{
	JointTransform result;
	XMStoreFloat3( &result.translation, XMVectorLerp( XMLoadFloat3( &a.translation ), XMLoadFloat3( &b.translation ), weight ) );
	XMStoreFloat3( &result.scale, XMVectorLerp( XMLoadFloat3( &a.scale ), XMLoadFloat3( &b.scale ), weight ) );
	const XMVECTOR qa = XMLoadFloat4( &a.rotation );
	XMVECTOR qb = XMLoadFloat4( &b.rotation );
	// q и −q — один поворот: смешиваются по кратчайшей дуге
	if( XMVectorGetX( XMVector4Dot( qa, qb ) ) < 0.0f )
		qb = XMVectorNegate( qb );
	XMStoreFloat4( &result.rotation, XMQuaternionNormalize( XMVectorLerp( qa, qb, weight ) ) );
	return result;
}

void blendPoses( const std::vector<JointTransform>& a, const std::vector<JointTransform>& b, float weight,
				 std::vector<JointTransform>& out )
{
	out.resize( a.size() );
	for( size_t i = 0; i < a.size(); ++i )
		out[i] = blendJoint( a[i], b[i], weight );
}

void restPose( const Skeleton& skeleton, std::vector<JointTransform>& out )
{
	out.resize( skeleton.jointCount() );
	for( size_t i = 0; i < out.size(); ++i )
		out[i] = skeleton.joint( i ).rest;
}

XMMATRIX jointMatrix( const JointTransform& joint )
{
	return XMMatrixScaling( joint.scale.x, joint.scale.y, joint.scale.z ) *
		   XMMatrixRotationQuaternion( XMLoadFloat4( &joint.rotation ) ) *
		   XMMatrixTranslation( joint.translation.x, joint.translation.y, joint.translation.z );
}

void modelSpace( const Skeleton& skeleton, const std::vector<JointTransform>& local, std::vector<XMFLOAT4X4>& model )
{
	model.resize( local.size() );
	for( size_t i = 0; i < local.size(); ++i )
	{
		XMMATRIX matrix = jointMatrix( local[i] );
		const int32_t parent = skeleton.joint( i ).parent;
		if( parent >= 0 )
			matrix = matrix * XMLoadFloat4x4( &model[parent] );
		XMStoreFloat4x4( &model[i], matrix );
	}
}

void skinningMatrices( const Skeleton& skeleton, const std::vector<XMFLOAT4X4>& model, XMFLOAT3X4* out )
{
	const std::vector<XMFLOAT4X4>& inverseBind = skeleton.inverseBind();
	for( size_t i = 0; i < model.size(); ++i )
		// XMStoreFloat3x4 пишет транспонированную матрицу: строки — столбцы аффинной
		XMStoreFloat3x4( &out[i], XMLoadFloat4x4( &inverseBind[i] ) * XMLoadFloat4x4( &model[i] ) );
}

}
