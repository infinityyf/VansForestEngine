#pragma once

class VansConfigration
{
private:
	static VansConfigration* instance;

	VansConfigration();

public:
	static VansConfigration* GetInstance();
};
