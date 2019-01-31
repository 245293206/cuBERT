#include <fstream>
#include <iostream>
#include <stdexcept>

#include "tensorflow/core/framework/graph.pb.h"
#include "cuBERT/common.h"
#include "BertM.h"

namespace cuBERT {
    BertM::BertM(const char *model_file,
                       size_t max_batch_size,
                       size_t seq_length,
                       size_t num_hidden_layers,
                       size_t num_attention_heads)
            : rr(0), graph(model_file) {
        int count = cuBERT::get_gpu_count();
        std::cout << "Found GPU count: " << count << std::endl;

        if (count == 0) {
            if (cuBERT::gpu()) {
                throw std::invalid_argument("No GPU device detected, but caller choose to use gpu.");
            } else {
                std::cout << "Use CPU instead" << std::endl;
                count++;
            }
        }

        for (int device = 0; device < count; ++device) {
            cuBERT::set_gpu(device);

            auto *bert = new Bert(graph.var, max_batch_size, seq_length,
                                  graph.vocab_size,
                                  graph.type_vocab_size,
                                  graph.hidden_size,
                                  num_hidden_layers,
                                  num_attention_heads,
                                  graph.intermediate_size);
            bert_instances.push_back(bert);

            mutex_instances.push_back(new std::mutex());

            std::cout << "device setup: " << device << std::endl;
        }
    }

    BertM::~BertM() {
        for (auto &bert_instance : bert_instances) {
            delete bert_instance;
        }
        for (auto &mutex_instance : mutex_instances) {
            delete mutex_instance;
        }
    }

    unsigned int BertM::compute_cpu(size_t batch_size, int *input_ids, char *input_mask, char *segment_ids, float *logits) {
        uint8_t count = rr++;
        unsigned int choice = count % bert_instances.size();

        cuBERT::set_gpu(choice);
        Bert *bert_instance = bert_instances[choice];
        std::mutex *mutex_instance = mutex_instances[choice];

        std::lock_guard<std::mutex> lg(*mutex_instance);
        bert_instance->compute(batch_size, input_ids, input_mask, segment_ids);
        bert_instance->logits(batch_size, logits);

        return choice;
    }
}