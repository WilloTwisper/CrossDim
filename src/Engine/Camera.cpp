#include "Camera.h"

using namespace DirectX;

Camera::Camera() {
    Position = XMFLOAT3(0.0f, 1.5f, 0.0f); 
    Rotation = XMFLOAT3(0.0f, 0.0f, 0.0f); 
    
    m_Forward = XMFLOAT3(0.0f, 0.0f, 1.0f);
    m_Right   = XMFLOAT3(1.0f, 0.0f, 0.0f);
    m_Up      = XMFLOAT3(0.0f, 1.0f, 0.0f);
}


void Camera::Update() {
    float pitch = XMConvertToRadians(Rotation.x);
    float yaw   = XMConvertToRadians(Rotation.y);
    float roll  = XMConvertToRadians(Rotation.z);

    XMMATRIX rotationMatrix = XMMatrixRotationRollPitchYaw(pitch, yaw, roll);

    XMVECTOR forward = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
    forward = XMVector3TransformCoord(forward, rotationMatrix);
    XMStoreFloat3(&m_Forward, XMVector3Normalize(forward));

    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    XMVECTOR right = XMVector3Cross(up, forward);
    XMStoreFloat3(&m_Right, XMVector3Normalize(right));
    
    up = XMVector3Cross(forward, right);
    XMStoreFloat3(&m_Up, XMVector3Normalize(up));
}

XMMATRIX Camera::GetViewMatrix() const {
    XMVECTOR pos = XMLoadFloat3(&Position);
    XMVECTOR forward = XMLoadFloat3(&m_Forward);
    XMVECTOR up = XMLoadFloat3(&m_Up);
    XMVECTOR target = XMVectorAdd(pos, forward);
    return XMMatrixLookAtLH(pos, target, up);
}

XMMATRIX Camera::GetProjectionMatrix(float fov, float aspectRatio, float nearZ, float farZ) const {
    return XMMatrixPerspectiveFovLH(XMConvertToRadians(fov), aspectRatio, nearZ, farZ);
}

void Camera::Rotate(float dx, float dy, float sensitivity) {
    Rotation.y += dx * sensitivity;
    Rotation.x += dy * sensitivity;

    if (Rotation.x > 89.0f) Rotation.x = 89.0f;
    if (Rotation.x < -89.0f) Rotation.x = -89.0f;
    
    if (Rotation.y > 360.0f) Rotation.y -= 360.0f;
    if (Rotation.y < 0.0f)   Rotation.y += 360.0f;
}