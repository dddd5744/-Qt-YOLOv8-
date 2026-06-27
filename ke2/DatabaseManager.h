#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H

#include <QString>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>
#include <QDateTime>

// ================= 数据库管理类 =================
// 功能：
//   1. 训练结果保存（模型路径、性能指标、参数）
//   2. 推理结果保存（图片路径、预测类别、置信度）
//   3. 标注数据持久化
class DatabaseManager
{
public:
    static bool init(const QString &dbPath = "annotation_data.db")
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE");
        db.setDatabaseName(dbPath);
        if (!db.open()) {
            qDebug() << "数据库打开失败:" << db.lastError().text();
            return false;
        }

        // 创建训练记录表
        QSqlQuery query;
        bool ok = query.exec(
            "CREATE TABLE IF NOT EXISTS training_records ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "dataset_path TEXT,"
            "epochs INTEGER,"
            "batch_size INTEGER,"
            "model_path TEXT,"
            "best_map REAL,"
            "train_time TEXT,"
            "created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
            ")"
        );
        if (!ok) qDebug() << "建表 training_records 失败:" << query.lastError().text();

        // 创建推理记录表
        ok = query.exec(
            "CREATE TABLE IF NOT EXISTS inference_records ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "image_path TEXT,"
            "predicted_class TEXT,"
            "confidence REAL,"
            "model_type TEXT,"
            "inference_mode TEXT,"
            "created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
            ")"
        );
        if (!ok) qDebug() << "建表 inference_records 失败:" << query.lastError().text();

        // 创建标注数据表
        ok = query.exec(
            "CREATE TABLE IF NOT EXISTS annotation_records ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "image_path TEXT,"
            "class_id INTEGER,"
            "x REAL,"
            "y REAL,"
            "width REAL,"
            "height REAL,"
            "updated_at DATETIME DEFAULT CURRENT_TIMESTAMP"
            ")"
        );
        if (!ok) qDebug() << "建表 annotation_records 失败:" << query.lastError().text();

        return true;
    }

    // ---- 训练记录 ----
    static bool saveTrainingRecord(const QString &datasetPath, int epochs, int batchSize,
                                    const QString &modelPath, float bestMap,
                                    const QString &trainTime)
    {
        QSqlQuery query;
        query.prepare("INSERT INTO training_records "
                      "(dataset_path, epochs, batch_size, model_path, best_map, train_time) "
                      "VALUES (?, ?, ?, ?, ?, ?)");
        query.addBindValue(datasetPath);
        query.addBindValue(epochs);
        query.addBindValue(batchSize);
        query.addBindValue(modelPath);
        query.addBindValue(bestMap);
        query.addBindValue(trainTime);
        if (!query.exec()) {
            qDebug() << "保存训练记录失败:" << query.lastError().text();
            return false;
        }
        return true;
    }

    static QList<QVariantMap> getTrainingRecords()
    {
        QList<QVariantMap> list;
        QSqlQuery query("SELECT * FROM training_records ORDER BY id DESC");
        while (query.next()) {
            QVariantMap map;
            map["id"] = query.value("id");
            map["dataset_path"] = query.value("dataset_path");
            map["epochs"] = query.value("epochs");
            map["batch_size"] = query.value("batch_size");
            map["model_path"] = query.value("model_path");
            map["best_map"] = query.value("best_map");
            map["train_time"] = query.value("train_time");
            list.append(map);
        }
        return list;
    }

    // ---- 推理记录 ----
    static bool saveInferenceRecord(const QString &imagePath, const QString &predClass,
                                     float confidence, const QString &modelType,
                                     const QString &mode)
    {
        QSqlQuery query;
        query.prepare("INSERT INTO inference_records "
                      "(image_path, predicted_class, confidence, model_type, inference_mode) "
                      "VALUES (?, ?, ?, ?, ?)");
        query.addBindValue(imagePath);
        query.addBindValue(predClass);
        query.addBindValue(confidence);
        query.addBindValue(modelType);
        query.addBindValue(mode);
        if (!query.exec()) {
            qDebug() << "保存推理记录失败:" << query.lastError().text();
            return false;
        }
        return true;
    }

    static QList<QVariantMap> getInferenceRecords()
    {
        QList<QVariantMap> list;
        QSqlQuery query("SELECT * FROM inference_records ORDER BY id DESC LIMIT 100");
        while (query.next()) {
            QVariantMap map;
            map["id"] = query.value("id");
            map["image_path"] = query.value("image_path");
            map["predicted_class"] = query.value("predicted_class");
            map["confidence"] = query.value("confidence");
            map["model_type"] = query.value("model_type");
            map["inference_mode"] = query.value("inference_mode");
            list.append(map);
        }
        return list;
    }

    // ---- 标注数据 ----
    static bool saveAnnotations(const QString &imagePath, const QStringList &labelsData)
    {
        QSqlQuery query;
        query.prepare("DELETE FROM annotation_records WHERE image_path = ?");
        query.addBindValue(imagePath);
        query.exec();

        for (const QString &line : labelsData) {
            QStringList parts = line.split(" ", Qt::SkipEmptyParts);
            if (parts.size() >= 5) {
                query.prepare("INSERT INTO annotation_records "
                              "(image_path, class_id, x, y, width, height) VALUES (?, ?, ?, ?, ?, ?)");
                query.addBindValue(imagePath);
                query.addBindValue(parts[0].toInt());
                query.addBindValue(parts[1].toFloat());
                query.addBindValue(parts[2].toFloat());
                query.addBindValue(parts[3].toFloat());
                query.addBindValue(parts[4].toFloat());
                if (!query.exec())
                    qDebug() << "标注保存失败:" << query.lastError().text();
            }
        }
        return true;
    }

    static QStringList loadAnnotations(const QString &imagePath)
    {
        QStringList result;
        QSqlQuery query;
        query.prepare("SELECT class_id, x, y, width, height FROM annotation_records WHERE image_path = ?");
        query.addBindValue(imagePath);
        if (query.exec()) {
            while (query.next()) {
                result.append(QString("%1 %2 %3 %4 %5")
                              .arg(query.value(0).toInt())
                              .arg(query.value(1).toDouble())
                              .arg(query.value(2).toDouble())
                              .arg(query.value(3).toDouble())
                              .arg(query.value(4).toDouble()));
            }
        }
        return result;
    }
};

#endif // DATABASEMANAGER_H
