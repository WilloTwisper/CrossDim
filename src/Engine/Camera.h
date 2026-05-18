#pragma once
#include <DirectXMath.h>

class Camera {
public:
    Camera();
    DirectX::XMFLOAT3 GetForward() const { return m_Forward; }

    void Update();
    void Move(float forward, float right, float speed);
    void Rotate(float dx, float dy, float sensitivity);
    DirectX::XMMATRIX GetViewMatrix() const;
    DirectX::XMMATRIX GetProjectionMatrix(float fov, float aspectRatio, float nearZ, float farZ) const;

    DirectX::XMFLOAT3 Position;
    DirectX::XMFLOAT3 Rotation;

private:
    DirectX::XMFLOAT3 m_Forward;
    DirectX::XMFLOAT3 m_Right;
    DirectX::XMFLOAT3 m_Up;
};