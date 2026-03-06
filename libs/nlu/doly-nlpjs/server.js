/**
 * Doly NLU Service - NLP.js 服务器
 * 
 * 功能:
 * - 加载训练好的 NLP.js 模型
 * - 提供 REST API 进行意图识别
 * - 支持健康检查和状态查询
 * 
 * API 端点:
 *   POST /parse         - 解析文本，返回意图
 *   POST /process       - 解析文本（/parse 的别名），返回意图
 *   GET  /health        - 健康检查
 *   GET  /stats         - 统计信息
 *   POST /reload        - 重新加载模型
 * 
 * 使用方法:
 *   node server.js [port] [model_path]
 */

const { NlpManager } = require('node-nlp');
const express = require('express');
const fs = require('fs');
const path = require('path');

// 配置
const PORT = parseInt(process.argv[2]) || 3000;
const MODEL_PATH = process.argv[3] || './model.nlp';

// 统计信息
const stats = {
    startTime: Date.now(),
    requests: 0,
    avgResponseTime: 0,
    lastRequest: null,
};

// 创建 Express 应用
const app = express();
app.use(express.json());

// NLP Manager 实例
let manager = null;
let modelLoaded = false;

/**
 * 加载模型
 */
async function loadModel() {
    console.log(`📦 Loading model from: ${MODEL_PATH}`);
    
    if (!fs.existsSync(MODEL_PATH)) {
        console.error(`❌ Model file not found: ${MODEL_PATH}`);
        console.log('💡 Please run "node train.js" first to generate the model.');
        return false;
    }
    
    manager = new NlpManager({ languages: ['zh'] });
    manager.load(MODEL_PATH);
    
    modelLoaded = true;
    console.log('✅ Model loaded successfully');
    return true;
}

/**
 * POST /parse - 解析文本
 * 
 * Request Body:
 *   { "text": "用户输入文本" }
 * 
 * Response:
 *   {
 *     "intent": "意图名称",
 *     "confidence": 0.95,
 *     "answer": "预设回复",
 *     "entities": [...],
 *     "raw_text": "原始文本",
 *     "process_time_ms": 5
 *   }
 */
app.post('/parse', async (req, res) => {
    const startTime = Date.now();
    stats.requests++;
    stats.lastRequest = new Date().toISOString();
    
    const text = req.body.text || '';
    
    if (!text) {
        return res.status(400).json({
            error: 'Missing "text" in request body',
            code: 'INVALID_REQUEST'
        });
    }
    
    if (!modelLoaded) {
        return res.status(503).json({
            error: 'Model not loaded',
            code: 'MODEL_NOT_READY'
        });
    }
    
    try {
        const result = await manager.process('zh', text);
        const processTime = Date.now() - startTime;
        
        // 更新平均响应时间
        stats.avgResponseTime = (
            (stats.avgResponseTime * (stats.requests - 1) + processTime) / stats.requests
        );
        
        // 构建响应
        const response = {
            intent: result.intent || 'unknown',
            confidence: result.score || 0,
            answer: result.answer || null,
            entities: result.entities || [],
            sentiment: result.sentiment || null,
            raw_text: text,
            process_time_ms: processTime
        };
        
        console.log(`[${new Date().toISOString()}] "${text}" → ${response.intent} (${(response.confidence * 100).toFixed(1)}%, ${processTime}ms)`);
        
        res.json(response);
    } catch (e) {
        console.error(`Error processing: ${e.message}`);
        res.status(500).json({
            error: 'Processing failed',
            code: 'PROCESS_ERROR',
            message: e.message
        });
    }
});

/**
 * POST /process - 解析文本（/parse 的别名）
 * 
 * 这是 /parse 的别名，提供兼容性。功能完全相同。
 * 
 * Request Body:
 *   { "text": "用户输入文本" }
 * 
 * Response:
 *   {
 *     "intent": "意图名称",
 *     "confidence": 0.95,
 *     "answer": "预设回复",
 *     "entities": [...],
 *     "raw_text": "原始文本",
 *     "process_time_ms": 5
 *   }
 */
app.post('/process', async (req, res) => {
    // 转发到 /parse 处理逻辑
    try {
        const startTime = Date.now();
        stats.requests++;
        stats.lastRequest = new Date().toISOString();
        
        const text = req.body.text || '';
        
        if (!text) {
            return res.status(400).json({
                error: 'Missing "text" in request body',
                code: 'INVALID_REQUEST'
            });
        }
        
        if (!modelLoaded) {
            return res.status(503).json({
                error: 'Model not loaded',
                code: 'MODEL_NOT_READY'
            });
        }
        
        const result = await manager.process('zh', text);
        const processTime = Date.now() - startTime;
        
        // 更新平均响应时间
        stats.avgResponseTime = (
            (stats.avgResponseTime * (stats.requests - 1) + processTime) / stats.requests
        );
        
        // 构建响应
        const response = {
            intent: result.intent || 'unknown',
            confidence: result.score || 0,
            answer: result.answer || null,
            entities: result.entities || [],
            sentiment: result.sentiment || null,
            raw_text: text,
            process_time_ms: processTime
        };
        
        console.log(`[${new Date().toISOString()}] "${text}" → ${response.intent} (${(response.confidence * 100).toFixed(1)}%, ${processTime}ms) [via /process]`);
        
        res.json(response);
    } catch (e) {
        console.error(`Error processing: ${e.message}`);
        res.status(500).json({
            error: 'Processing failed',
            code: 'PROCESS_ERROR',
            message: e.message
        });
    }
});

/**
 * GET /health - 健康检查
 */
app.get('/health', (req, res) => {
    res.json({
        status: modelLoaded ? 'healthy' : 'unhealthy',
        model_loaded: modelLoaded,
        uptime_seconds: Math.floor((Date.now() - stats.startTime) / 1000),
        version: '1.0.0'
    });
});

/**
 * GET /stats - 统计信息
 */
app.get('/stats', (req, res) => {
    res.json({
        requests: stats.requests,
        avg_response_time_ms: stats.avgResponseTime.toFixed(2),
        uptime_seconds: Math.floor((Date.now() - stats.startTime) / 1000),
        last_request: stats.lastRequest,
        model_path: MODEL_PATH,
        model_loaded: modelLoaded
    });
});

/**
 * POST /reload - 重新加载模型
 */
app.post('/reload', async (req, res) => {
    console.log('🔄 Reloading model...');
    
    const success = await loadModel();
    
    if (success) {
        res.json({ status: 'ok', message: 'Model reloaded successfully' });
    } else {
        res.status(500).json({ status: 'error', message: 'Failed to reload model' });
    }
});

/**
 * 启动服务器
 */
async function start() {
    console.log('='.repeat(60));
    console.log(' Doly NLU Service - NLP.js Server');
    console.log('='.repeat(60));
    
    // 加载模型
    await loadModel();
    
    // 启动服务器
    app.listen(PORT, '0.0.0.0', () => {
        console.log(`\n🚀 Server running on http://0.0.0.0:${PORT}`);
        console.log('\n📡 API Endpoints:');
        console.log(`   POST /parse    - Parse text and get intent`);
        console.log(`   POST /process  - Parse text (alias for /parse)`);
        console.log(`   GET  /health   - Health check`);
        console.log(`   GET  /stats    - Statistics`);
        console.log(`   POST /reload   - Reload model`);
        console.log('\n' + '='.repeat(60));
    });
}

// 启动
start();
