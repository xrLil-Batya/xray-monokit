#pragma once
enum class eEngineModes
{
	eModeVanilla,
	eModeGunsDev,
};

class ENGINE_API CEngineMode
{
private:
	eEngineModes engine_mode;

public:
	void LoadMode();
	IC eEngineModes GetMode() { return engine_mode; }
};

extern ENGINE_API CEngineMode* EngineMode();