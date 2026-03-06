/**
 * Doly NLU Service - NLP.js 训练脚本
 * 
 * 功能:
 * - 从 JSON 语料库文件加载意图和训练数据
 * - 训练 NLP.js 模型
 * - 保存模型到指定位置
 * 
 * 使用方法:
 *   node train.js [model_path]
 *   model_path: 可选，模型保存路径，默认为 ./model.nlp
 */

const { NlpManager } = require('node-nlp');
const fs = require('fs');
const path = require('path');

// 配置
const CORPUS_DIR = path.join(__dirname, '../../demo/config/nlu_corpus/intents');
const DEFAULT_MODEL_PATH = './model.nlp';

async function loadCorpusFiles(corpusDir) {
    const intents = [];
    
    // 读取语料库目录中的所有 JSON 文件
    const files = fs.readdirSync(corpusDir).filter(f => f.endsWith('.json'));
    
    console.log(`📂 Found ${files.length} corpus files in ${corpusDir}`);
    
    for (const file of files) {
        const filePath = path.join(corpusDir, file);
        try {
            const content = fs.readFileSync(filePath, 'utf-8');
            const data = JSON.parse(content);
            
            if (data.intents && Array.isArray(data.intents)) {
                console.log(`  📄 ${file}: ${data.intents.length} intents`);
                intents.push(...data.intents);
            }
        } catch (e) {
            console.error(`  ❌ Error loading ${file}: ${e.message}`);
        }
    }
    
    return intents;
}

async function train() {
    const modelPath = process.argv[2] || DEFAULT_MODEL_PATH;
    
    console.log('='.repeat(60));
    console.log(' Doly NLU - NLP.js 模型训练');
    console.log('='.repeat(60));
    
    // 创建 NLP Manager
    const manager = new NlpManager({
        languages: ['zh'],
        forceNER: true,
        nlu: {
            log: false,
            useNoneFeature: true,
        }
    });
    
    // 加载语料库
    console.log('\n📚 Loading corpus...');
    const intents = await loadCorpusFiles(CORPUS_DIR);
    
    if (intents.length === 0) {
        console.error('❌ No intents found! Please check corpus directory.');
        process.exit(1);
    }
    
    // 添加训练数据
    console.log('\n📝 Adding training data...');
    let totalUtterances = 0;
    
    for (const intent of intents) {
        const intentName = intent.intent;
        const utterances = intent.utterances || [];
        
        for (const utterance of utterances) {
            manager.addDocument('zh', utterance, intentName);
            totalUtterances++;
        }
    }
    
    console.log(`   Total intents: ${intents.length}`);
    console.log(`   Total utterances: ${totalUtterances}`);
    
    // 添加一些预设回复（可选）
    console.log('\n💬 Adding default answers...');
    manager.addAnswer('zh', 'greeting.hello', '你好！我是 Doly，很高兴见到你！');
    manager.addAnswer('zh', 'greeting.bye', '再见！下次见！');
    manager.addAnswer('zh', 'greeting.thanks', '不客气！很高兴能帮到你！');
    manager.addAnswer('zh', 'self.name', '我叫 Doly，是一个可爱的桌面 AI 机器人！');
    manager.addAnswer('zh', 'self.ability', '我会报时、播放音乐、讲笑话、讲故事，还能陪你聊天！');
    
    // 训练模型
    console.log('\n🏋️ Training model...');
    console.time('Training time');
    await manager.train();
    console.timeEnd('Training time');
    
    // 保存模型
    console.log(`\n💾 Saving model to: ${modelPath}`);
    manager.save(modelPath);
    
    // 验证模型
    console.log('\n🧪 Quick validation...');
    const testCases = [
        '你好',
        '关机',
        '今天天气怎么样',
        '讲个笑话',
        '你叫什么名字',
    ];
    
    for (const text of testCases) {
        const result = await manager.process('zh', text);
        console.log(`   "${text}" → ${result.intent} (${(result.score * 100).toFixed(1)}%)`);
    }
    
    console.log('\n' + '='.repeat(60));
    console.log(' ✅ Training completed successfully!');
    console.log('='.repeat(60));
}

// 运行
train().catch(e => {
    console.error('Training failed:', e);
    process.exit(1);
});
