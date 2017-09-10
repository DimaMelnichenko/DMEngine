#pragma once
#include <vector>

class DMModel;
class DMShader;

class DMRenderQueue
{
public:
	DMRenderQueue();
	~DMRenderQueue();

	bool inititlise( DMShader*, int phase );
	DMShader* shader();
	int phase();

	void append( DMModel* );
	void clear();
	std::vector<DMModel*>& queue();

private:
	std::vector<DMModel*> m_queue;
	DMShader *m_shader;
	int m_phase;
};

